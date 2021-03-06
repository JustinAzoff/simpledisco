//  --------------------------------------------------------------------------
//  Example Zyre distributed chat application
//
//  Copyright (c) 2010-2014 The Authors
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//  --------------------------------------------------------------------------


#include "zyre.h"
#include "zsimpledisco.h"

const char *getenv_with_default(const char *key, const char *def)
{
    const char *val = getenv(key);
    return val ? val : def;
}

char *
public_key_from_endpoint(char *endpoint)
{
    char *pipe = strchr(endpoint, '|');
    char *public_key = NULL;
    if(pipe != NULL) {
        *pipe = '\0';
        public_key = pipe+1;
    }
    return public_key;
}

void
bootstrap_simpledisco(zsimpledisco_t *disco, zcertstore_t *certstore)
{

    zlistx_t *certs = zcertstore_certs(certstore);
    zcert_t *cert = (zcert_t *) zlistx_first(certs);
    int cert_count = 0;
    int endpoint_count = 0;
    while (cert) {
        const char *endpoint = zcert_meta (cert, "simpledisco-endpoint");
        const char *public_key = zcert_public_txt(cert);
        if(endpoint) {
            char *real_endpoint = zsys_sprintf("%s|%s", endpoint, public_key);
            zsys_info("gateway: Connecting to simpledisco server @ %s using %s", endpoint, public_key);
            zsimpledisco_connect(disco, real_endpoint);
            zstr_free(&real_endpoint);
            endpoint_count++;
        }
        cert = (zcert_t *) zlistx_next(certs);
        cert_count++;
    }

    if(cert_count==0)
        zsys_error("gateway: No certs found in certstore");
    else if(endpoint_count==0)
        zsys_error("gateway: No certs found in certstore that contain simpledisco-endpoint metadata");
    zlistx_destroy(&certs);
}

void
maybe_create_untrusted_key(
    zcertstore_t *certstore, zcertstore_t *certstore_untrusted,
    const char *trusted_path, const char *untrusted_path,
    const char *public_key)
{
    zcert_t *cert;
    cert = zcertstore_lookup(certstore, public_key);
    if(cert)
        return;

    cert = zcertstore_lookup(certstore_untrusted, public_key);
    if(cert)
        return;

    int file_num;
    char *trusted_filename;
    char *untrusted_filename;
    for(file_num = 1 ; file_num < 1000 ; file_num++){
        trusted_filename = zsys_sprintf("%s/discovered_%03d.key", trusted_path, file_num);
        untrusted_filename = zsys_sprintf("%s/discovered_%03d.key", untrusted_path, file_num);
        if (!zsys_file_exists(trusted_filename) && !zsys_file_exists(untrusted_filename))
            break;
        zstr_free(&trusted_filename);
        zstr_free(&untrusted_filename);
    }
    assert(untrusted_filename);

    zsys_debug("gateway: Discovered public_key: %s, adding to %s", public_key, untrusted_filename);
    cert = zcert_new_from_txt(public_key, "");
    zcert_save_public(cert, untrusted_filename);
    zstr_free(&trusted_filename);
    zstr_free(&untrusted_filename);
}

static void 
gateway_actor (zsock_t *pipe, void *args)
{
    int64_t last_bootstrap = 0;
    int64_t last_zyre_dump = 0;

    const char *endpoint = getenv_with_default(
        "ZYRE_BIND", "tcp://*:5670");

    const char *pubsub_endpoint = getenv_with_default(
        "PUBSUB_ENDPOINT", "tcp://127.0.0.1:14000");
    const char *control_endpoint = getenv_with_default(
        "CONTROL_ENDPOINT", "tcp://127.0.0.1:14001");

    const char *private_key_path = getenv_with_default(
        "PRIVATE_KEY_PATH", "client.key_secret");
    const char *public_key_dir_path = getenv_with_default(
        "PUBLIC_KEY_DIR_PATH", "./public_keys");
    const char *untrusted_public_key_dir_path = getenv_with_default(
        "UNTRUSTED_PUBLIC_KEY_DIR_PATH", "./public_keys_untrusted");


    assert(!zsys_dir_create(public_key_dir_path));
    assert(!zsys_dir_create(untrusted_public_key_dir_path));

    zcertstore_t *certstore = zcertstore_new(public_key_dir_path);
    assert(certstore);

    zcertstore_t *certstore_untrusted = zcertstore_new(untrusted_public_key_dir_path);
    assert(certstore_untrusted);

    zsock_t *pub = zsock_new(ZMQ_PUB);
    zsock_t *control = zsock_new(ZMQ_ROUTER);

    if (-1 == zsock_bind(pub, "%s", pubsub_endpoint)) {
        fprintf(stderr, "Faild to bind to PUBSUB_ENDPOINT %s", pubsub_endpoint);
        perror(" ");
        exit(1);
    }
    if (-1 == zsock_bind(control, "%s", control_endpoint)) {
        fprintf(stderr, "Faild to bind to CONTROL_ENDPOINT %s", control_endpoint);
        perror(" ");
        exit(1);
    }

    zsimpledisco_t *disco = zsimpledisco_new();
    zsimpledisco_verbose(disco);

    zcert_t *cert = NULL;
    if(private_key_path) {
        zsimpledisco_set_private_key_path(disco, private_key_path);
        cert = zcert_load(private_key_path);

        zactor_t *auth = zactor_new (zauth,NULL);
        zstr_send(auth,"VERBOSE");
        zsock_wait(auth);
        zstr_sendx (auth, "CURVE", public_key_dir_path, NULL);
        zsock_wait(auth);
    }

    zyre_t *node = zyre_new ((char *) args);
    if (!node)
        return;                 //  Could not create new node

    //FIXME: The order of the next few lines matters a lot for some reason
    //I should be able to start the node after the setup, but that isn't working
    //because self->inbox gets hosed somehow
    //zyre_set_verbose (node);
    zclock_sleep(1000);
    if(cert) {
        zyre_set_zcert(node, cert);
    }
    zyre_start (node);
    zyre_set_endpoint(node, "%s", endpoint);
    const char *uuid = zyre_uuid (node);
    printf("My uuid is %s\n", uuid);
    if(cert) {
        char *published_endpoint = zsys_sprintf("%s|%s", endpoint, zcert_public_txt(cert));
        zsimpledisco_publish(disco, published_endpoint, uuid);
    } else {
        zsimpledisco_publish(disco, endpoint, uuid);
    }
    //zyre_join (node, "CHAT");
    zsock_signal (pipe, 0);     //  Signal "ready" to caller

    //zclock_sleep(1000);

    bool terminated = false;

    zpoller_t *poller = zpoller_new (pipe, zyre_socket (node), zsimpledisco_socket(disco), control, NULL);
    while (!terminated) {
        void *which = zpoller_wait (poller, 5000);
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (which);
            if (!msg)
                break;              //  Interrupted

            char *command = zmsg_popstr (msg);
            if (streq (command, "$TERM"))
                terminated = true;
            else {
                puts ("E: invalid message to actor");
                assert (false);
            }
            free (command);
            zmsg_destroy (&msg);
        }
        else
        if (which == zyre_socket (node)) {
            zmsg_t *msg = zmsg_recv (which);
            char *event = zmsg_popstr (msg);
            char *peer = zmsg_popstr (msg);
            char *name = zmsg_popstr (msg);
            char *group = zmsg_popstr (msg);
            char *message = zmsg_popstr (msg);

            if (streq (event, "SHOUT")) {
                //zsys_debug("zyre->pub %s: %s: %s", group, name, message);
                zstr_sendx (pub, group, name, message, NULL);
            }
            else
            if (streq (event, "ENTER"))
                zsys_info("%s has joined the network", name);
            else
            if (streq (event, "EXIT"))
                zsys_info("%s has left the network", name);

            free (event);
            free (peer);
            free (name);
            free (group);
            free (message);
            zmsg_destroy (&msg);
        }
        else
        if (which == zsimpledisco_socket (disco)) {
            zmsg_t *msg = zmsg_recv (which);
            char *new_endpoint = zmsg_popstr (msg);
            char *new_uuid = zmsg_popstr (msg);
            zsys_debug("Discovered peer: uuid='%s' endpoint='%s'", new_uuid, new_endpoint);
            char *public_key = public_key_from_endpoint(new_endpoint);
            if(strneq(endpoint, new_endpoint) && strneq(uuid, new_uuid)) {
                zyre_require_peer (node, new_uuid, new_endpoint, public_key);
                maybe_create_untrusted_key(certstore, certstore_untrusted, public_key_dir_path, untrusted_public_key_dir_path, public_key);
            }
            free (new_endpoint);
            free (new_uuid);
            zmsg_destroy (&msg);
        }
        else
        if (which == control) {
            zmsg_t *msg = zmsg_recv (which);
            //zsys_debug("Got message from control socket");
            //zmsg_print(msg);
            zframe_t *routing_id = zmsg_pop(msg);
            char *command = zmsg_popstr (msg);
            if (streq (command, "SUB")) {
                char *group = zmsg_popstr (msg);
                zsys_debug("Joining %s", group);
                zyre_join (node, group);
                free(group);
            }
            else
            if (streq (command, "PUB")) {
                char *group = zmsg_popstr (msg);
                char *str = zmsg_popstr (msg);
                //zsys_debug("pub->zyre %s: %s", group, str);
                zyre_shouts (node, group, "%s", str);
                zstr_sendx (pub, group, "local", str, NULL);
                free(group);
                free(str);
            }
            zframe_destroy(&routing_id);
            zstr_free(&command);
            zmsg_destroy(&msg);
        }

        if(zclock_mono() - last_bootstrap > 30*1000) {
            bootstrap_simpledisco(disco, certstore);
            last_bootstrap = zclock_mono();
        }
        if(zclock_mono() - last_zyre_dump > 60*1000) {
            zyre_print(node);
            last_zyre_dump = zclock_mono();
        }


    }
    zpoller_destroy (&poller);
    zyre_stop (node);
    zclock_sleep (100);
    zyre_destroy (&node);
}

int
gateway_cmd (char *node_name)
{
    zactor_t *actor = zactor_new (gateway_actor, node_name);
    assert (actor);
    
    while (!zsys_interrupted) {
        zclock_sleep(1000);
    }
    zactor_destroy (&actor);
    return 0;
}
