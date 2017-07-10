#include "czmq_library.h"
#include "zsimpledisco.h"

static int
zsimpledisco_dump_hash(zhash_t *h)
{
    char *item;
    int64_t now = zclock_mono();
    for (item = zhash_first (h); item != NULL; item = zhash_next (h)) {
        const char *key = zhash_cursor (h);
        zsys_debug("Discovered data: key='%s' value='%s'", key, item);
    }
    return 0;
}

int main(int argn, char *argv[])
{
    if(argn < 2) {
        fprintf(stderr, "Usage: %s tcp://127.0.0.1:9999 tcp://127.0.0.1:9998\n", argv[0]);
        exit(1);
    }
    zactor_t *server1 = zactor_new (zsimpledisco, "disco");
    assert (server1);
    zstr_send (server1, "VERBOSE");
    for(int n=1;n<argn;n++) {
        zstr_sendx (server1, "CONNECT", argv[n], NULL);
    }

    char *key_str = zsys_sprintf ("Client-%d", getpid());

    zstr_sendx (server1, "PUBLISH", key_str, "Hello", NULL);

    while(1) {
        zclock_sleep (5000);
        zstr_send (server1, "VALUES");
        zframe_t *data = zframe_recv(server1);
        zhash_t *h = zhash_unpack(data);

        zsimpledisco_dump_hash(h);
    }

    zactor_destroy(&server1);
}
