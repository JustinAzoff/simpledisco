#include "czmq_library.h"
#include "zsimpledisco.h"

struct _zsimpledisco_t {
    zactor_t *actor;            //  A zsimpledisco instance wraps the actor instance
    zsock_t *inbox;             //  Receives incoming cluster traffic
};

//  --------------------------------------------------------------------------
//  The self_t structure holds the state for one actor instance
typedef struct {
    zsock_t *pipe;              //  Actor command pipe
    zsock_t *outbox;            //  Outbox back to application
    bool terminated;            //  Did caller ask us to quit?
    bool verbose;               //  Verbose logging enabled?
    zsock_t *server_socket;     //  Socket for talking to clients
    int64_t last_cleanup;       //  Time records were last cleaned up
    int64_t last_send;          //  Time records were last sent
    int64_t last_deliver;       //  Time records were last delivered out of the actor
    int64_t last_reconnect;     //  Time of last reconnect attempt
    int send_interval;          //  Interval to re-send data to the server
    int deliver_interval;        //  Interval to deliver data
    int cleanup_interval;       //  Cleanup interval in seconds
    int cleanup_max_age;        //  Cleanup records older than this many seconds
    int reconnect_interval;     //  Interval to reconnect to unreachable hosts
    int peer_timeout;           //  Timeout for peer socket.
    zhash_t *data;              //  key/value data, on the server
    zhash_t *client_data;       //  key/value data, on the client
    zhash_t *client_sockets;    //  endpoint/socket mapping of client sockets
    zlist_t *reconnect_queue;   //  List of endpoints to attempt to reconnect to

    zactor_t *auth;             //  zauth Actor, if curve enabled
    zcertstore_t *certstore;    //  certstore, for verififying connections
    zcert_t *private_key;       //  curve private key
} self_t;

typedef struct {
    char *value;
    int64_t ts;
} value_t;

void
value_t_free(void *item_p)
{
    assert(item_p);
    value_t *item = item_p;
    free(item->value);
    free(item);
    item=NULL;
}

zsimpledisco_t *
zsimpledisco_new()
{
    zsimpledisco_t *self = (zsimpledisco_t *) zmalloc (sizeof (zsimpledisco_t));
    assert (self);

    //  Create front-to-back pipe pair for data traffic
    zsock_t *outbox;
    self->inbox = zsys_create_pipe (&outbox);

    //  Start node engine and wait for it to be ready
    self->actor = zactor_new (zsimpledisco_actor, outbox);
    assert (self->actor);

    return self;
}

void
zsimpledisco_destroy (zsimpledisco_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zsimpledisco_t *self = *self_p;
        zactor_destroy (&self->actor);
		zsock_destroy (&self->inbox);
        freen (self);
        *self_p = NULL;
    }
}

void
zsimpledisco_connect(zsimpledisco_t *self, const char *endpoint)
{
	zstr_sendx (self->actor, "CONNECT", endpoint, NULL);
}

void
zsimpledisco_bind(zsimpledisco_t *self, const char *endpoint)
{
	zstr_sendx (self->actor, "BIND", endpoint, NULL);
}

void
zsimpledisco_verbose(zsimpledisco_t *self)
{
	zstr_sendx (self->actor, "VERBOSE", NULL);
}

int
zsimpledisco_set_certstore_path(zsimpledisco_t *self, const char *path)
{
	return zstr_sendx (self->actor, "SET CERTSTORE PATH", path, NULL);
}
int
zsimpledisco_set_private_key_path(zsimpledisco_t *self, const char *path)
{
	return zstr_sendx (self->actor, "SET PRIVATE KEY PATH", path, NULL);
}

void
zsimpledisco_publish(zsimpledisco_t *self, const char *key, const char *value)
{
	zstr_sendx (self->actor, "PUBLISH", key, value, NULL);
}
void
zsimpledisco_get_values(zsimpledisco_t *self)
{
	zstr_sendx (self->actor, "GET VALUES", NULL);
}


//  --------------------------------------------------------------------------
//  Return node zsock_t socket, for direct polling of socket

zsock_t *
zsimpledisco_socket (zsimpledisco_t *self)
{
    assert (self);
    return self->inbox;
}

//Helpers

int
zsimpledisco_dump_hash(zhash_t *h)
{
    value_t *val;
    int64_t now = zclock_mono();
    for (val = zhash_first (h); val != NULL; val = zhash_next (h)) {
        const char *key = zhash_cursor (h);
        zsys_info("zsimpledisco: key='%s' value='%s' ts='%ld' age='%ld'", key, val->value, val->ts, (now-val->ts) / 1000);
    }
    return 0;
}
zhash_t *
convert_hash(zhash_t *h)
{
    zhash_t *kv = zhash_new();
    zhash_autofree(kv);
    value_t *val;
    for (val = zhash_first (h); val != NULL; val = zhash_next (h)) {
        const char *key = zhash_cursor (h);
        //zsys_debug("zsimpledisco: Creating new hash with just %s=%s", key, val->value);
        zhash_update(kv, key, val->value);
    }
    return kv;
}

static void
s_self_destroy (self_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        self_t *self = *self_p;
        if (self->server_socket) // don't close STDIN
            zsock_destroy (&self->server_socket);
        zsock_destroy (&self->outbox);
        zhash_destroy(&self->data);
        zhash_destroy(&self->client_data);
        zhash_destroy(&self->client_sockets); //disconnect first?
        zlist_destroy(&self->reconnect_queue);
        if(self->auth)
            zactor_destroy (&self->auth);
        if(self->certstore)
            zcertstore_destroy(&self->certstore);
        if(self->private_key)
            zcert_destroy(&self->private_key);
        freen (self);
        *self_p = NULL;
    }
}

static self_t *
s_self_new (zsock_t *pipe)
{
    self_t *self = (self_t *) zmalloc (sizeof (self_t));
    assert (self);
    self->pipe = pipe;

    self->server_socket = zsock_new (ZMQ_ROUTER);
    self->deliver_interval = 30 * 1000;
    self->cleanup_interval = 5 * 1000;
    self->cleanup_max_age = 60 * 1000;
    self->send_interval = self->cleanup_max_age - 2 * self->cleanup_interval;
    self->reconnect_interval = 90 * 1000;
    self->peer_timeout = 2 * 1000;

    self->data = zhash_new();
    self->client_data = zhash_new();
    self->client_sockets = zhash_new();
    self->reconnect_queue = zlist_new();
    zlist_autofree(self->reconnect_queue);

    return self;
}


// Client Stuff

static void
s_self_refresh_data(self_t *self)
{
    // Send new values immediately
    self->last_send = 0;

    // Deliver 2 seconds later
    self->last_deliver = zclock_mono() - self->deliver_interval + 2000 ;
}

// Callback for removing items from the client_sockets hash
static void
s_zsocket_free(void *argument)
{
    zsock_t *socket = (zsock_t *) argument;
    zsock_destroy(&socket);
}

static int
s_self_connect(self_t *self, const char *endpoint)
{
    // Ignore if we already have a connection for this endpoint
    // Unifying inital connections and reconnections will make this not needed.
    void *val = zhash_lookup(self->client_sockets, endpoint);
    if (val)
        return 0;
    if (self->verbose)
        zsys_debug("zsimpledisco: Client wants to connect to %s", endpoint);

    char *public_key = NULL;
    char *endpoint_copy = strdup(endpoint);
    char *pipe = strchr(endpoint_copy, '|');
    if(pipe != NULL) {
        *pipe = '\0';
        public_key = pipe+1;
    }

    zsock_t * sock =  zsock_new (ZMQ_DEALER);
    zsock_set_sndtimeo(sock, self->peer_timeout);
    zsock_set_rcvtimeo(sock, self->peer_timeout);

    if(self->private_key && public_key) {
        //zsys_debug("zsimpledisco: Connecting to endpoint %s with public key %s", endpoint_copy, public_key);
        zcert_apply (self->private_key, sock);
        zsock_set_curve_serverkey (sock, public_key);
    }

    if(-1 == zsock_connect(sock, "%s", endpoint_copy)) {
        zsys_error("Invalid endpoint %s", endpoint_copy);
        return -1;
    }

    zhash_update (self->client_sockets, endpoint, sock);
    zhash_freefn (self->client_sockets, endpoint, s_zsocket_free);
    free(endpoint_copy);
    return 0;
}

static int
s_self_connect_initial(self_t *self, const char *endpoint)
{
    void *val = zhash_lookup(self->client_sockets, endpoint);
    if(val)
        return 0;
    int ret =  s_self_connect(self, endpoint);

    s_self_refresh_data(self);
    return ret;
}

static int
s_self_client_reconnect_all(self_t *self)
{
    const char *endpoint = (const char *) zlist_first (self->reconnect_queue);
    while (endpoint) {
        if (self->verbose)
            zsys_debug ("zsimpledisco: reconnecting to %s!", endpoint);
        s_self_connect(self, endpoint);
        endpoint = (const char *) zlist_next (self->reconnect_queue);
    }
    zlist_purge(self->reconnect_queue);
    return 0;
}

static int
s_self_client_reconnect_later(self_t *self, const char *endpoint)
{
    if (self->verbose)
        zsys_debug ("zsimpledisco: reconnect to %s later", endpoint);
    int ret = zlist_append(self->reconnect_queue, (void *)endpoint);
    zhash_delete (self->client_sockets, endpoint);
    return ret;
}

static int
s_self_client_publish(self_t *self, char *key, char *value)
{
    zsock_t *sock;
    for (sock = zhash_first (self->client_sockets); sock != NULL; sock = zhash_next (self->client_sockets)) {
        const char *endpoint = zhash_cursor (self->client_sockets);
        if (self->verbose)
            zsys_debug("zsimpledisco: PUBLISH %s => '%s' '%s'", endpoint, key, value);
        if(-1 == zstr_sendx(sock, "PUBLISH", key, value, NULL)) {
            if (self->verbose)
                zsys_info("zsimpledisco: send to %s failed", endpoint);
        }
        //TODO: this should do scatter/gather kind of thing
        char *response = zstr_recv(sock);
        if(response) {
            zstr_free(&response);
        } else {
            if (self->verbose)
                zsys_info("zsimpledisco: no response from %s", endpoint);
            s_self_client_reconnect_later(self, endpoint);
        }
    }
    return 0;
}

static int
s_self_client_publish_all(self_t *self)
{
    zsock_t *sock;
    char * value;
    for (sock = zhash_first (self->client_sockets); sock != NULL; sock = zhash_next (self->client_sockets)) {
        const char *endpoint = zhash_cursor (self->client_sockets);
        for (value = zhash_first (self->client_data); value != NULL; value = zhash_next (self->client_data)) {
            const char *key = zhash_cursor (self->client_data);
            if (self->verbose)
                zsys_debug("zsimpledisco: PUBLISH %s => '%s' '%s'", endpoint, key, value);
            if(-1 == zstr_sendx(sock, "PUBLISH", key, value, NULL)) {
                if (self->verbose)
                    zsys_info("zsimpledisco: send to %s failed", endpoint);
            }
            //TODO: this should do scatter/gather kind of thing
            char *response = zstr_recv(sock);
            if(response) {
                zstr_free(&response);
            } else {
                if (self->verbose)
                    zsys_info("zsimpledisco: no response from %s", endpoint);
                s_self_client_reconnect_later(self, endpoint);
                break;
            }
        }
    }
    return 0;

}

static void
zsimpledisco_merge_hash(zhash_t *dest, zhash_t *src)
{
    void *val;
    for (val = zhash_first (src); val != NULL; val = zhash_next (src)) {
        const char *key = zhash_cursor (src);
        //zsys_debug("zsimpledisco: Adding %s to new merged hash", key);
        zhash_update (dest, key, val);
    }
}

static int
s_self_client_get_values(self_t *self, zhash_t *merged)
{
    zsock_t *sock;
    for (sock = zhash_first (self->client_sockets); sock != NULL; sock = zhash_next (self->client_sockets)) {
        const char *endpoint = zhash_cursor (self->client_sockets);
        if (self->verbose)
            zsys_debug("zsimpledisco: Send %s => 'VALUES'", endpoint);
        if(-1 == zstr_send(sock, "VALUES")) {
            if (self->verbose)
                zsys_info("zsimpledisco: send to %s failed", endpoint);
        }
        //TODO: this should do scatter/gather kind of thing
        zframe_t *data = zframe_recv(sock);
        if(data) {
            zhash_t *h = zhash_unpack(data);
            zsimpledisco_merge_hash(merged, h);
            zhash_destroy(&h);
            zframe_destroy(&data);
        } else {
            if (self->verbose)
                zsys_info("zsimpledisco: no response from %s", endpoint);
            s_self_client_reconnect_later(self, endpoint);
        }
    }
    return 0;
}


// Server Stuff

int
s_self_set_certstore_path(self_t *self, const char *path)
{
    if(self->verbose)
        zsys_info("zsimpledisco: Certificate directory: %s", path);
    if(self->auth == NULL) {
        //Start authenticator
        zactor_t *auth = zactor_new (zauth,NULL);
        self->auth = auth;
        if (self->verbose) {
            zstr_send(auth,"VERBOSE");
            zsock_wait(auth);
        }
    }
    //  Tell the authenticator to use the certificate store in ./certs
    zstr_sendx (self->auth, "CURVE", path, NULL);

    if(self->certstore) {
        zcertstore_destroy(&self->certstore);
    }
    self->certstore = zcertstore_new(path);
    return 0;
}

int
s_self_set_private_key_path(self_t *self, const char *path)
{
    if(self->verbose)
        zsys_info("zsimpledisco: Using private key: %s", path);

    self->private_key = zcert_load(path);
    if(!self->private_key) {
        zsys_error("zsimpledisco: unable to load private key from %s", path);
        return 1;
    }
    return 0;
}

int
s_self_bind(self_t *self, const char *endpoint)
{
    if(self->verbose)
        zsys_info("zsimpledisco: binding to %s", endpoint);

    if(self->private_key) {
        zcert_apply (self->private_key, self->server_socket);
        zsock_set_curve_server (self->server_socket, 1);
        assert (zsock_mechanism (self->server_socket) == ZMQ_CURVE);
    }

    return -1 == zsock_bind (self->server_socket, "%s", endpoint);
}

static int
s_self_add_kv(self_t *self, const char *key, char *value)
{
    value_t *record = (value_t *) zmalloc (sizeof (value_t));
    record->value = strdup(value);
    record->ts = zclock_mono();
    zhash_update (self->data, key, record);
    zhash_freefn (self->data, key, value_t_free);
    return 0;
}

static int
s_self_handle_server_socket (self_t *self)
{
    zframe_t *routing_id = zframe_recv (self->server_socket);
    zframe_t *command_frame = zframe_recv(self->server_socket);
    char *command = zframe_strdup(command_frame);
    const char *peer_address = zframe_meta(command_frame, "Peer-Address");
    if(self->certstore) {
        const char *peer_public_key = zframe_meta(command_frame, "User-Id");
        if(!zcertstore_lookup(self->certstore, peer_public_key)) {
            if (self->verbose)
                zsys_info("zsimpledisco: Peer key %s no longer in certstore, ignoring.", peer_public_key);
            goto out;
        }
    }

    if (self->verbose)
        zsys_info ("zsimpledisco: server peer=%s command=%s", peer_address ? peer_address: "", command);
    if (streq (command, "PUBLISH")) {
        char *key = zstr_recv(self->server_socket);
        char *value = zstr_recv(self->server_socket);
        if(key && strlen(key) > 8 && key[6] == '*') {
            char *new_key = zsys_sprintf("tcp://%s%s", peer_address, &key[7]);
            if (self->verbose)
                zsys_debug("zsimpledisco: Rewrote %s to %s", key, new_key);
            zstr_free(&key);
            key = new_key;
        }
        if (self->verbose)
            zsys_info ("zsimpledisco: server PUBLISH '%s' '%s'", key, value);
        s_self_add_kv(self, key, value);
        zstr_free (&key);
        zstr_free (&value);
        zframe_send (&routing_id, self->server_socket, ZFRAME_MORE);
        if(-1 == zstr_send(self->server_socket, "OK")) {
            if (self->verbose)
                zsys_info("zsimpledisco: send failed");
        }
    }
    else
    if (streq (command, "VALUES")) {
        zframe_send (&routing_id, self->server_socket, ZFRAME_MORE);
        zhash_t *kvhash = convert_hash(self->data);
        zframe_t *all_data =  zhash_pack (kvhash);
        zframe_send (&all_data, self->server_socket, 0);
        zhash_destroy(&kvhash);
    }

out:
    zstr_free (&command);
    zframe_destroy(&command_frame);
    return 0;
}

static int
s_self_handle_expire_data(self_t *self)
{
    zlist_t *keys_to_delete = zlist_new();

    value_t *item;
    int64_t now = zclock_mono();
    int64_t expiration_cuttoff = now - self->cleanup_max_age;
    for (item = zhash_first (self->data); item != NULL; item = zhash_next (self->data)) {
        const char *key = zhash_cursor (self->data);
        if(item->ts < expiration_cuttoff) {
            if (self->verbose)
                zsys_debug("zsimpledisco: expire key='%s' value='%s' ts='%ld' age='%ld'", key, item->value, item->ts, (now-item->ts) / 1000);
            if(-1 == zlist_append(keys_to_delete, (void *)key)) {
                zsys_error("zsimpledisco: zlist_append failed");
            }
        }
    }
    const char *del = (const char *) zlist_first (keys_to_delete);
    while (del) {
        zhash_delete(self->data, del);
        del = (const char *) zlist_next (keys_to_delete);
    }
    zlist_destroy(&keys_to_delete);
    return 0;
}

static int
s_self_handle_cleanup(self_t *self)
{
    //zsimpledisco_dump_hash(self->data);
    s_self_handle_expire_data(self);

    return 0;
}

// Common stuff

static int
s_self_handle_pipe (self_t *self)
{
    //  Get just the command off the pipe
    char *command = zstr_recv (self->pipe);
    if (!command)
        return -1;                  //  Interrupted

    if (self->verbose)
        zsys_info ("zsimpledisco: API command=%s", command);

    if (streq (command, "VERBOSE"))
        self->verbose = true;
    else
    if (streq (command, "SET CERTSTORE PATH")) {
        char *path = zstr_recv (self->pipe);
        s_self_set_certstore_path(self, path);
        zstr_free(&path);
    }
    else
    if (streq (command, "SET PRIVATE KEY PATH")) {
        char *path = zstr_recv (self->pipe);
        if(s_self_set_private_key_path(self, path))
            assert(false);//FIXME: right way to signal fatal error from inside an actor?
        zstr_free(&path);
    }
    else
    if (streq (command, "BIND")) {
        char *endpoint = zstr_recv (self->pipe);
        if(s_self_bind(self, endpoint)) {
            zsys_error ("could not bind to %s", endpoint);
            self->terminated = true;
            assert(false);//FIXME: right way to signal fatal error from inside an actor?
        }
        zstr_free(&endpoint);
    }
    else
    if (streq (command, "CONNECT")) {
        char *endpoint = zstr_recv (self->pipe);
        s_self_connect_initial(self, endpoint);
        zstr_free(&endpoint);
    }
    else
    if (streq (command, "PUBLISH")) {
        char *key = zstr_recv(self->pipe);
        char *value = zstr_recv(self->pipe);
        zhash_update (self->client_data, key, value);
        s_self_client_publish(self, key, value);
    }
    else
    if (streq (command, "GET VALUES")) {
        self->last_deliver = 0;
    }
    else
    if (streq (command, "$TERM"))
        self->terminated = true;
    else {
        zsys_error ("zsimpledisco: - invalid command: %s", command);
        assert (false);
    }
    zstr_free (&command);
    return 0;
}

void
s_self_deliver_all (self_t *self)
{
    zhash_t *h = zhash_new();
    zhash_autofree(h);
    s_self_client_get_values(self, h);
    char *val;
    for (val = zhash_first (h); val != NULL; val = zhash_next (h)) {
        const char *key = zhash_cursor (h);
        //zsys_debug("zsimpledisco: key='%s' value='%s', key, val);
        zstr_sendx(self->outbox, key, val, NULL);
    }
    zhash_destroy(&h);
}

void
zsimpledisco_actor (zsock_t *pipe, void *args)
{
    self_t *self = s_self_new (pipe);
    assert (self);
    //  Signal successful initialization
    zsock_signal (pipe, 0);

    self->pipe = pipe;
    self->outbox = (zsock_t *) args;

    zpoller_t *poller = zpoller_new (NULL);
    zpoller_add (poller, self->pipe);
    zpoller_add (poller, self->server_socket);

    while (!self->terminated) {
        alarm(120);
        zsock_t *which = (zsock_t *) zpoller_wait (poller, 1000);
        if(which == self->pipe) {
            s_self_handle_pipe (self);
        }
        if(which == self->server_socket) {
            s_self_handle_server_socket(self);
        }

        if(zpoller_expired(poller)) {
            //zsys_debug ("zsimpledisco: Idle");
        }
        if(zclock_mono() - self->last_deliver > self->deliver_interval) {
            s_self_deliver_all(self);
            self->last_deliver = zclock_mono();
        }

        if(zclock_mono() - self->last_cleanup > self->cleanup_interval) {
            s_self_handle_cleanup(self);
            self->last_cleanup = zclock_mono();
        }
        if(zclock_mono() - self->last_send > self->send_interval) {
            s_self_client_publish_all(self);
            self->last_send = zclock_mono();
        }
        if(zclock_mono() - self->last_reconnect > self->reconnect_interval) {
            s_self_client_reconnect_all(self);
            self->last_reconnect = zclock_mono();
        }
    }
    alarm(0);
    s_self_destroy(&self);
}
