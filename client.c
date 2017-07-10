#include "czmq_library.h"
#include "zsimpledisco.h"

int main(int argn, char *argv[])
{
    if(argn < 2) {
        fprintf(stderr, "Usage: %s tcp://127.0.0.1:9999 tcp://127.0.0.1:9998\n", argv[0]);
        exit(1);
    }

    zsimpledisco_t *disco = zsimpledisco_new();
    zsimpledisco_verbose(disco);
    for(int n=1;n<argn;n++) {
        zsimpledisco_connect(disco, argv[n]);
    }

    char *key_str = zsys_sprintf ("Client-%d", getpid());

    zsimpledisco_publish(disco, key_str, "Hello");

    zpoller_t *poller = zpoller_new (NULL);
    zpoller_add(poller, zsimpledisco_socket(disco));

    while(1) {
        zsock_t *which = zpoller_wait (poller, 1000);
        if (which == zsimpledisco_socket (disco)) {
            zmsg_t *msg = zmsg_recv (which);
            char *key = zmsg_popstr (msg);
            char *value = zmsg_popstr (msg);
            printf("KEY VALUE PAIR: '%s' '%s'\n", key, value);
            free (key);
            free (value);
            zmsg_destroy (&msg);
        }
    }
    zsimpledisco_destroy(&disco);
}
