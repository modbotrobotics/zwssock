#ifndef ZWSHANDSHAKE_H_
#define ZWSHANDSHAKE_H_

#include <czmq.h>

typedef struct _zwshandshake_t zwshandshake_t;

zwshandshake_t* zwshandshake_new();

void zwshandshake_destroy(zwshandshake_t** self_p);

bool zwshandshake_parse_request(zwshandshake_t* self, zframe_t* data);

zframe_t* zwshandshake_get_response(zwshandshake_t* self, unsigned char* client_compression_factor, unsigned char* server_compression_factor);

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif  // ZWSHANDSHAKE_H_
