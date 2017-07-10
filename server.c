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
    zsimpledisco_bind(disco, bind);
    while(1) {
        zclock_sleep (1000);
    }
    zsimpledisco_destroy(&disco);
}
