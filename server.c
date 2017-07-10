#include "czmq_library.h"
#include "zsimpledisco.h"

int main(int argn, char *argv[])
{
    if(argn < 2) {
        fprintf(stderr, "Usage: %s tcp://*:9999\n", argv[0]);
        exit(1);
    }
    char* bind = argv[1];
    zactor_t *server1 = zactor_new (zsimpledisco, "disco");
    assert (server1);
    zstr_send (server1, "VERBOSE");
    zstr_sendx (server1, "BIND", bind, NULL);
    while(1) {
        zclock_sleep (1000);
    }
    zactor_destroy(&server1);
}
