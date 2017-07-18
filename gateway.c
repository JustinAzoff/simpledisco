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

static void 
gateway_actor (zsock_t *pipe, void *args)
{
    const char *disco_server = getenv("DISCO_SERVER");
    const char *endpoint = getenv("ZYRE_BIND");
    const char *private_key_path = getenv("PRIVATE_KEY_PATH");
    const char *public_key_dir_path = getenv("PUBLIC_KEY_DIR_PATH");

    const char *pubsub_endpoint = getenv("PUBSUB_ENDPOINT");
    const char *control_endpoint = getenv("CONTROL_ENDPOINT");

    if(!pubsub_endpoint)
        pubsub_endpoint = "tcp://127.0.0.1:14000";
    if(!control_endpoint)
        control_endpoint = "tcp://127.0.0.1:14001";
    if(!public_key_dir_path)
        public_key_dir_path = "./public_keys";

    zsock_t *pub = zsock_new(ZMQ_PUB);
    zsock_t *control = zsock_new(ZMQ_ROUTER);

    if (-1 == zsock_bind(pub, pubsub_endpoint)) {
        fprintf(stderr, "Faild to bind to PUBSUB_ENDPOINT %s", pubsub_endpoint);
        perror(" ");
        exit(1);
    }
    if (-1 == zsock_bind(control, control_endpoint)) {
        fprintf(stderr, "Faild to bind to CONTROL_ENDPOINT %s", control_endpoint);
        perror(" ");
        exit(1);
    }


    if(!disco_server) {
        fprintf(stderr, "export DISCO_SERVER=tcp://localhost:9100\n");
        exit(1);
    }

    if(!endpoint) {
        fprintf(stderr, "export ZYRE_BIND=tcp://*:9200\n");
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

    zsimpledisco_connect(disco, disco_server);

    zyre_t *node = zyre_new ((char *) args);
    if (!node)
        return;                 //  Could not create new node

    zyre_set_verbose (node);
    zyre_start (node);
    zclock_sleep(1000);
    if(cert) {
        zyre_set_curve_keypair(node, zcert_public_txt(cert), zcert_secret_txt(cert));
    }
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
    zsimpledisco_get_values(disco);

    bool terminated = false;

    zpoller_t *poller = zpoller_new (pipe, zyre_socket (node), zsimpledisco_socket(disco), control, NULL);
    while (!terminated) {
        void *which = zpoller_wait (poller, -1);
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
                zsys_debug("zyre->pub %s: %s: %s", group, name, message);
                zstr_sendx (pub, group, name, message, NULL);
            }

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
            char *key = zmsg_popstr (msg);
            char *value = zmsg_popstr (msg);
            zsys_debug("Discovered data: key='%s' value='%s'", key, value);
            if(strneq(endpoint, key) && strneq(uuid, value)) {
                zyre_require_peer (node, value, key);
            }
            free (key);
            free (value);
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
                zsys_debug("pub->zyre %s: %s", group, str);
                zyre_shouts (node, group, "%s", str);
                zstr_sendx (pub, group, "local", str, NULL);
                free(group);
                free(str);
            }
            zframe_destroy(&routing_id);
            zmsg_destroy(&msg);
        }
    }
    zpoller_destroy (&poller);
    zyre_stop (node);
    zclock_sleep (100);
    zyre_destroy (&node);
}

int keygen()
{
    const char *keypair_filename = "client.key";
    const char *keypair_filename_secret = "client.key_secret";

    if( access( keypair_filename, F_OK ) != -1 ) {
        fprintf(stderr, "%s already exists\n", keypair_filename);
        return 1;
    }
    if( access( keypair_filename_secret, F_OK ) != -1 ) {
        fprintf(stderr, "%s already exists\n", keypair_filename);
        return 1;
    }
    zcert_t *cert = zcert_new();
    if(!cert) {
        perror("Error creating new certificate");
        return 1;
    }
    if(-1 == zcert_save(cert, keypair_filename)) {
        perror("Error writing key to client.key");
        return 1;
    }
    printf("Keys written to %s and %s\n", keypair_filename, keypair_filename_secret);
    return 0;
}

int
main (int argc, char *argv [])
{

    if (argc > 2) {
        puts ("syntax: ./gateway [node_name|keygen]");
        exit (1);
    }
    if (argc == 2 && streq(argv[1], "keygen")) {
        exit(keygen());
    }

    char *disco_server = getenv("DISCO_SERVER");
    if(!disco_server) {
        fprintf(stderr, "Missing DISCO_SERVER env var:\nexport DISCO_SERVER=tcp://localhost:9100\n");
        exit(1);
    }

    char *endpoint = getenv("ZYRE_BIND");
    if(!endpoint) {
        fprintf(stderr, "Missing ZYRE_BIND env var:\nexport ZYRE_BIND=tcp://*:9200\n");
        exit(1);
    }

    zactor_t *actor = zactor_new (gateway_actor, argv [1]);
    assert (actor);
    
    while (!zsys_interrupted) {
        zclock_sleep(1000);
    }
    zactor_destroy (&actor);
    return 0;
}
