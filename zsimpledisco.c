#include "czmq_library.h"

//  --------------------------------------------------------------------------
//  The self_t structure holds the state for one actor instance

typedef struct {
    zsock_t *pipe;              //  Actor command pipe
    bool terminated;            //  Did caller ask us to quit?
    bool verbose;               //  Verbose logging enabled?
    zsock_t *server_socket;     //  Socket for talking to clients

} self_t;

static void
s_self_destroy (self_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        self_t *self = *self_p;
        if (self->server_socket) // don't close STDIN
            zsock_destroy (&self->server_socket);
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

    return self;
}

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
    if (streq (command, "BIND")) {
        char *endpoint = zstr_recv (self->pipe);
        if(-1 == zsock_bind (self->server_socket, "%s", endpoint))
            zsys_warning ("could not bind to %s", endpoint);
        zstr_free(&endpoint);
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

static int
s_self_handle_server_socket (self_t *self)
{
    zframe_t *routing_id = zframe_recv (self->server_socket);
    zframe_print(routing_id, "zsimpledisco: frame");
    const char *peer_address = zframe_meta(routing_id, "Peer-Address");
    if(peer_address) {
        zsys_debug("zsimpledisco: Peer Address is %s", peer_address);
        //zstr_free(&peer_address); //FIXME: CRASHES
    }
    char *command = zstr_recv(self->server_socket);

    if (self->verbose)
        zsys_info ("zsimpledisco: server command=%s", command);
    if (streq (command, "PUBLISH")) {
        char *key = zstr_recv(self->server_socket);
        char *value = zstr_recv(self->server_socket);
        zsys_info ("zsimpledisco: server PUBLISH '%s' '%s'", key, value);
        zstr_free (&key);
        zstr_free (&value);
        zframe_send (&routing_id, self->server_socket, ZFRAME_MORE + ZFRAME_REUSE);
        zstr_send(self->server_socket, "OK");
    }
    else
    if (streq (command, "VALUES")) {
        zsys_info ("zsimpledisco: handle VALUES");
        zframe_send (&routing_id, self->server_socket, ZFRAME_MORE + ZFRAME_REUSE);
        zstr_send(self->server_socket, "[]");
    }
    zstr_free (&command);
    return 0;
}


void
zsimpledisco (zsock_t *pipe, void *args)
{
    self_t *self = s_self_new (pipe);
    assert (self);
    //  Signal successful initialization
    zsock_signal (pipe, 0);

    zpoller_t *poller = zpoller_new (NULL);
    zpoller_add (poller, self->pipe);
    zpoller_add (poller, self->server_socket);

    while (!self->terminated) {
        zsock_t *which = (zsock_t *) zpoller_wait (poller, 1000);
        if(which == self->pipe) {
            s_self_handle_pipe (self);
            zpoller_add (poller, self->server_socket);
        }
        if(which == self->server_socket) {
            s_self_handle_server_socket(self);
        }

        if(zpoller_expired(poller)) {
            zsys_debug ("zsimpledisco: Idle");
        }
    }
    s_self_destroy(&self);
}
