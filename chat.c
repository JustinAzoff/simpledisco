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

//  This actor will listen and publish anything received
//  on the CHAT group

static void 
chat_actor (zsock_t *pipe, void *args)
{
    char *disco_server = getenv("DISCO_SERVER");
    if(!disco_server) {
        fprintf(stderr, "export DISCO_SERVER=tcp://localhost:9100\n");
        exit(1);
    }
    zsimpledisco_t *disco = zsimpledisco_new();
    zsimpledisco_verbose(disco);
    zsimpledisco_connect(disco, disco_server);

    char *endpoint = getenv("ZYRE_BIND");
    if(!endpoint) {
        fprintf(stderr, "export ZYRE_BIND=tcp://127.0.0.1:9200\n");
        exit(1);
    }

    zyre_t *node = zyre_new ((char *) args);
    if (!node)
        return;                 //  Could not create new node

    zyre_set_verbose (node);
    zyre_set_endpoint(node, endpoint);
    zyre_start (node);
    const char *uuid = zyre_uuid (node);
    printf("My uuid is %s\n", uuid);
    zsimpledisco_publish(disco, endpoint, uuid);
    zyre_join (node, "CHAT");
    zsock_signal (pipe, 0);     //  Signal "ready" to caller

    zclock_sleep(1000);
    zsimpledisco_get_values(disco);

    bool terminated = false;
    zpoller_t *poller = zpoller_new (pipe, zyre_socket (node), zsimpledisco_socket(disco), NULL);
    while (!terminated) {
        void *which = zpoller_wait (poller, -1);
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (which);
            if (!msg)
                break;              //  Interrupted

            char *command = zmsg_popstr (msg);
            if (streq (command, "$TERM"))
                terminated = true;
            else
            if (streq (command, "SHOUT")) { 
                char *string = zmsg_popstr (msg);
                zyre_shouts (node, "CHAT", "%s", string);
            }
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

            if (streq (event, "ENTER"))
                printf ("%s has joined the chat\n", name);
            else
            if (streq (event, "EXIT"))
                printf ("%s has left the chat\n", name);
            else
            if (streq (event, "SHOUT"))
                printf ("%s: %s\n", name, message);
            else
            if (streq (event, "EVASIVE"))
                printf ("%s is being evasive\n", name);

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
            zyre_require_peer (node, value, key);
            free (key);
            free (value);
            zmsg_destroy (&msg);
        }
    }
    zpoller_destroy (&poller);
    zyre_stop (node);
    zclock_sleep (100);
    zyre_destroy (&node);
}

int
main (int argc, char *argv [])
{
    if (argc < 2) {
        puts ("syntax: ./chat myname");
        exit (0);
    }
    zactor_t *actor = zactor_new (chat_actor, argv [1]);
    assert (actor);
    
    while (!zsys_interrupted) {
        char message [1024];
        if (!fgets (message, 1024, stdin))
            break;
        message [strlen (message) - 1] = 0;     // Drop the trailing linefeed
        zstr_sendx (actor, "SHOUT", message, NULL);
    }
    zactor_destroy (&actor);
    return 0;
}
