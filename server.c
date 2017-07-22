#include "czmq_library.h"
#include "zsimpledisco.h"
#include "gateway.h"

int main(int argn, char *argv[])
{
    if(argn < 2) {
        fprintf(stderr, "Usage: %s tcp://*:9999\n", argv[0]);
        exit(1);
    }

    char* bind = argv[1];
    return server_cmd(bind);
}
