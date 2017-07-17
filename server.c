#include "czmq_library.h"
#include "zsimpledisco.h"

int main(int argn, char *argv[])
{
    if(argn < 2) {
        fprintf(stderr, "Usage: %s tcp://*:9999\n", argv[0]);
        exit(1);
    }
    char* bind = argv[1];

    zsimpledisco_t *disco = zsimpledisco_new();
    zsimpledisco_verbose(disco);

    const char *certstore_path = getenv("CERTSTORE_PATH");
    if(certstore_path) {
        zsimpledisco_set_certstore_path(disco, certstore_path);
    }
    const char *private_key_path = getenv("PRIVATE_KEY_PATH");
    if(private_key_path) {
        zsimpledisco_set_private_key_path(disco, private_key_path);
    }

    zsimpledisco_bind(disco, bind);

    zpoller_t *poller = zpoller_new (NULL);
    zpoller_add(poller, zsimpledisco_socket(disco));

    while(1) {
        zpoller_wait (poller, 1000);
        if(zpoller_terminated(poller))
            break;
    }
    zsimpledisco_destroy(&disco);
}
