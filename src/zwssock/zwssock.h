#ifndef ZWSSOCK_H_
#define ZWSSOCK_H_
#define LIBCZMQ_EXPORTS

#include <czmq.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zwssock_t zwssock_t;

CZMQ_EXPORT zwssock_t* zwssock_new_router();

CZMQ_EXPORT void zwssock_destroy(zwssock_t **self_p);

CZMQ_EXPORT int zwssock_bind(zwssock_t *self, char *endpoint);

CZMQ_EXPORT int zwssock_send(zwssock_t *self, zmsg_t **msg_p);

CZMQ_EXPORT zmsg_t * zwssock_recv(zwssock_t *self);

CZMQ_EXPORT void* zwssock_handle(zwssock_t *self);

#ifdef __cplusplus
}
#endif

#endif  // ZWSSOCK_H_
