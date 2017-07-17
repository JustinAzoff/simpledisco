#ifndef __ZSIMPLEDISCO_H_INCLUDED__
#define __ZSIMPLEDISCO_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zsimpledisco_t zsimpledisco_t;

CZMQ_EXPORT zsimpledisco_t *
    zsimpledisco_new();

CZMQ_EXPORT zsock_t *
    zsimpledisco_socket (zsimpledisco_t *self);

CZMQ_EXPORT void
    zsimpledisco_destroy (zsimpledisco_t **self_p);

CZMQ_EXPORT void
    zsimpledisco_actor (zsock_t *pipe, void *unused);

CZMQ_EXPORT void
    zsimpledisco_connect(zsimpledisco_t *self, const char *endpoint);

CZMQ_EXPORT void
    zsimpledisco_bind(zsimpledisco_t *self, const char *endpoint);

CZMQ_EXPORT void
    zsimpledisco_verbose(zsimpledisco_t *self);

CZMQ_EXPORT void
    zsimpledisco_publish(zsimpledisco_t *self, const char *key, const char* value);

CZMQ_EXPORT void
    zsimpledisco_get_values(zsimpledisco_t *self);

CZMQ_EXPORT int
    zsimpledisco_dump_hash(zhash_t *h);

CZMQ_EXPORT int
        zsimpledisco_set_certstore_path(zsimpledisco_t *self, const char *certstore_path);
CZMQ_EXPORT int
        zsimpledisco_set_private_key_path(zsimpledisco_t *self, const char *private_key_path);

#ifdef __cplusplus
}
#endif

#endif
