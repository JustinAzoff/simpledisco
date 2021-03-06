#include "czmq_library.h"
#include "zsimpledisco.h"
#include "gateway.h"

void usage(char *cmd)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "    %s [node_name]\n", cmd);
    fprintf(stderr, "    %s keygen\n", cmd);
    fprintf(stderr, "    %s disco tcp://*:9999\n\n", cmd);
    fprintf(stderr, "Environment Variables and their defaults:\n"
        "UNTRUSTED_PUBLIC_KEY_DIR_PATH  ./public_keys_untrusted path to directory to store new public keys\n"
        "PRIVATE_KEY_PATH     client.key_secret     path to private key\n"
        "PUBLIC_KEY_DIR_PATH  ./public_keys         path to directory containing public keys\n"
        "ZYRE_BIND            tcp://*:5670          the endpoint that the zyre p2p socket should bind to\n"
        "DISABLE_CURVE        unset                 set to disable curve encryption for sockets\n"
        "PUBSUB_ENDPOINT      tcp://127.0.0.1:14000 the endpoint that the gateway should bind to for pubsub\n" 
        "CONTROL_ENDPOINT     tcp://127.0.0.1:14001 the endpoint that the gateway should bind to for control\n"

    );
    exit (1);
}

int
main (int argc, char *argv [])
{
    zsys_init();

    const char *private_key_path = getenv("PRIVATE_KEY_PATH");
    if(!private_key_path) {
        private_key_path = "client.key_secret";
        zsys_info("PRIVATE_KEY_PATH defaulted to '%s'", private_key_path);
    }

    if (!zsys_file_exists(private_key_path)) {
        keygen_cmd(private_key_path);
    }

    if (argc == 2 && streq(argv[1], "keygen")) {
        exit(keygen_cmd(private_key_path));
    }

    if (argc == 2 && streq(argv[1], "disco")) {
        usage(argv[0]);
    }
    if (argc == 3 && streq(argv[1], "disco")) {
        exit(server_cmd(argv[2]));
    }

    char *hostname;
    if (argc == 2) {
        hostname = argv[1];
    } else {
        hostname = zsys_hostname();
        zsys_info("hostname defaulted to '%s'", hostname);
    }

    return gateway_cmd(hostname);
}
