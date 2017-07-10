#include "czmq_library.h"
#include "zsimpledisco.h"

int main(int argn, char *argv[])
{
    zactor_t *server1 = zactor_new (zsimpledisco, "disco");
    assert (server1);
    zstr_send (server1, "VERBOSE");
    zstr_sendx (server1, "BIND", "tcp://*:9101", NULL);
    zclock_sleep (3000);
    zactor_destroy(&server1);
}
