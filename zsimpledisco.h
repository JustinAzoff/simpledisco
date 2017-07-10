#ifndef __ZSIMPLEDISCO_H_INCLUDED__
#define __ZSIMPLEDISCO_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

CZMQ_EXPORT void
    zsimpledisco (zsock_t *pipe, void *unused);

CZMQ_EXPORT static int
    zsimpledisco_dump_hash(zhash_t *h);

#ifdef __cplusplus
}
#endif

#endif
