#ifndef __ZSIMPLEDISCO_H_INCLUDED__
#define __ZSIMPLEDISCO_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zsimpledisco_t zsimpledisco_t;

CZMQ_EXPORT zsimpledisco_t *
    zsimpledisco_new();

CZMQ_EXPORT void
    zsimpledisco_destroy (zsimpledisco_t **self_p);

CZMQ_EXPORT void
    zsimpledisco_actor (zsock_t *pipe, void *unused);

CZMQ_EXPORT int
    zsimpledisco_dump_hash(zhash_t *h);

CZMQ_EXPORT void
    zsimpledisco_connect(zsimpledisco_t *self, const char *endpoint);

CZMQ_EXPORT void
    zsimpledisco_bind(zsimpledisco_t *self, const char *endpoint);

CZMQ_EXPORT void
    zsimpledisco_verbose(zsimpledisco_t *self);

#ifdef __cplusplus
}
#endif

#endif
