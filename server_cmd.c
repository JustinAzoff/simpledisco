#include "czmq_library.h"
#include "zsimpledisco.h"
#include "gateway.h"

int server_cmd(char *bind_endpoint)
{
    zsimpledisco_t *disco = zsimpledisco_new();
    zsimpledisco_verbose(disco);

    const char *certstore_path = getenv("PUBLIC_KEY_DIR_PATH");
    const char *private_key_path = getenv("PRIVATE_KEY_PATH");
    const char *disable_curve = getenv("DISABLE_CURVE");

    if(!certstore_path) {
        certstore_path = "public_keys";
        zsys_info("zsimpledisco: PUBLIC_KEY_DIR_PATH defaulted to '%s'", certstore_path);
    }

    if(!private_key_path) {
        private_key_path = "client.key_secret";
        zsys_info("zsimpledisco: PRIVATE_KEY_PATH defaulted to '%s'", private_key_path);
    }

    if(!disable_curve) {
        if(keygen_cmd(private_key_path))
            return 1;
        zsys_info("zsimpledisco: Enabling curve crypto. Disable using DISABLE_CURVE=1");
        zsimpledisco_set_certstore_path(disco, certstore_path);
        zsimpledisco_set_private_key_path(disco, private_key_path);
    } else {
        zsys_info("zsimpledisco: curve crypto disabled using DISABLE_CURVE");
    }

    zsimpledisco_bind(disco, bind_endpoint);

    zpoller_t *poller = zpoller_new (NULL);
    zpoller_add(poller, zsimpledisco_socket(disco));

    while(1) {
        zpoller_wait (poller, 1000);
        if(zpoller_terminated(poller))
            break;
    }
    zsimpledisco_destroy(&disco);
    return 0;
}
