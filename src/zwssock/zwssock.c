#include "zwssock.h"
#include "zwshandshake.h"
#include "zwsdecoder.h"

#include <czmq.h>
#include <string.h>
#include <zlib.h>

#define ZWS_DEBUG false

#if ZWS_DEBUG
  #define ZWS_LOG_DEBUG(x) printf x
#else
  #define ZWS_LOG_DEBUG(x) (void)0
#endif

struct _zwssock_t {
	zactor_t* control_actor;              										//  Control to / from agent
	zsock_t* data;                 														//  Data to / from agent
};

//  This background thread does all the real work
static void s_agent_task(zsock_t* control, void* args);

/**
 *
*/


zwssock_t* zwssock_new_router() {
	zwssock_t* self = (zwssock_t *)zmalloc(sizeof(zwssock_t));

	assert(self);

	self->control_actor = zactor_new(s_agent_task, NULL);

	//  Create separate data socket, send address on control socket
	self->data = zsock_new(ZMQ_PAIR);
	assert(self->data);
	int rc = zsock_bind(self->data, "inproc://data-%p", self->data);
	assert(rc != -1);
	zstr_sendf(self->control_actor, "inproc://data-%p", self->data);

	return self;
}

/**
 *
*/
void zwssock_destroy(zwssock_t** self_p) {
	assert(self_p);
	if (*self_p) {
		zwssock_t* self = *self_p;
		zactor_destroy(&self->control_actor);

		zsock_destroy(&self->data);

		// free(zstr_recv(self->control_actor));
		free(self);
		*self_p = NULL;
	}
}

/**
 * Bind socket to endpoint address
*/
int zwssock_bind(zwssock_t* self, const char* endpoint) {
	assert(self);
	return zstr_sendx(self->control_actor, "BIND", endpoint, NULL);
}

/**
 * Send message over socket
*/
int zwssock_send(zwssock_t* self, zmsg_t** msg_p) {
	assert(self);
	assert(zmsg_size(*msg_p) > 0);

	return zmsg_send(msg_p, self->data);
}

/**
 * Receive message from socket
*/
zmsg_t* zwssock_recv(zwssock_t* self) {
	assert(self);
	zmsg_t* msg = zmsg_recv(self->data);
	return msg;
}

/**
 * Get internal ZSock handle
*/
zsock_t* zwssock_handle(zwssock_t* self) {
	assert(self);
	return self->data;
}


//  *************************    BACK END AGENT    *************************

typedef struct {
	zsock_t* control;              														// Control socket back to application
	zsock_t* data;                 														// Data socket to application
	zsock_t* stream;               														// Stream socket to server
	zhash_t* clients;           															// Known clients
} agent_t;

/**
 *
*/
static agent_t* s_agent_new(zsock_t* control) {
	agent_t* self = (agent_t *)zmalloc(sizeof(agent_t));
	self->control = control;
	self->stream = zsock_new(ZMQ_STREAM);

	//  Connect our data socket to caller's endpoint
	self->data = zsock_new(ZMQ_PAIR);
	char* endpoint = zstr_recv(self->control);
	int rc = zsock_connect(self->data, "%s", endpoint);
	assert(rc != -1);
	free(endpoint);

	self->clients = zhash_new();
	return self;
}

/**
 *
*/
static void s_agent_destroy(agent_t** self_p) {
	assert(self_p);
	if (*self_p) {
		agent_t* self = *self_p;
		zhash_destroy(&self->clients);
		zsock_destroy(&self->stream);
		zsock_destroy(&self->data);
		free(self);
		*self_p = NULL;
	}
}

/**
 * Client connection state
*/
typedef enum {
	CONNECTION_CLOSED = 0,
	CONNECTION_CONNECTED = 1,
	CONNECTION_EXCEPTION = 2
} connection_state_t;

/**
 * Client information
*/
typedef struct {
	agent_t* agent;             //  Client's agent
	connection_state_t state;   //  Current state
	zframe_t* address;          //  Client address identity
	char* hashkey;              //  Client hash key
	zwsdecoder_t* decoder;
	unsigned char client_compression_factor; // Requested compression factor by the server for the client
	unsigned char server_compression_factor; // Requested compression factor by the client for the server
	z_stream permessage_deflate_client;   // The client advertised permessage-deflate extension
	z_stream permessage_deflate_server;   // The server advertised permessage-deflate extension

	zmsg_t* outgoing_msg;		// Currently outgoing message, if not NULL final frame was not yet arrived
} client_t;

/**
 * Create new client
*/
static client_t* zwssock_client_new(agent_t* agent, zframe_t* address) {
	client_t* self = (client_t *)zmalloc(sizeof(client_t));
	assert(self);
	ZWS_LOG_DEBUG(("Creating new client for socket [%s] (%s)\n", zframe_strhex(address), zsock_endpoint(agent->stream)));
	self->agent = agent;
	self->address = zframe_dup(address);
	self->hashkey = zframe_strhex(address);
	self->state = CONNECTION_CLOSED;
	self->decoder = NULL;
	self->client_compression_factor = 10;
	self->server_compression_factor = 10;
	self->permessage_deflate_client.zalloc   = Z_NULL;
	self->permessage_deflate_client.zfree    = Z_NULL;
	self->permessage_deflate_client.opaque   = Z_NULL;
	self->permessage_deflate_client.avail_in = 0;
	self->permessage_deflate_client.next_in  = Z_NULL;
	self->permessage_deflate_server.zalloc   = Z_NULL;
	self->permessage_deflate_server.zfree    = Z_NULL;
	self->permessage_deflate_server.opaque   = Z_NULL;
	self->permessage_deflate_server.avail_in = 0;
	self->permessage_deflate_server.next_in  = Z_NULL;
	self->outgoing_msg = NULL;
	return self;
}

/**
 * Destroy client
*/
static void zwssock_client_destroy(client_t** self_p) {
	assert(self_p);
	if (*self_p) {
		client_t* self = *self_p;
		ZWS_LOG_DEBUG(("Destroying client [%s]\n", self->hashkey));
	
		zframe_destroy(&self->address);

		if (self->decoder != NULL) {
			zwsdecoder_destroy(&self->decoder);
		}

		if (self->client_compression_factor > 0) {
			inflateEnd(&self->permessage_deflate_client);
		}

		if (self->server_compression_factor > 0) {
			deflateEnd(&self->permessage_deflate_server);
		}

		if (self->outgoing_msg != NULL) {
			zmsg_destroy(&self->outgoing_msg);
		}

		free(self->hashkey);
		free(self);
		*self_p = NULL;
	}
}

#define CHUNK 8192

/**
 * Parse messages received from client, send them as ZMessages to the Server
 *
 * A single request from the JSMQ client is comprised of multiple messages, one for each frame.
 * Each message must be decompressed if the client supplied a decompression factor during handshake.
 * A final request is constructed from the parsed (and inflated) messages, and sent to the server.
*/
void zwssock_router_message_received(void* tag, byte* payload, int length) {
	client_t* self = (client_t *)tag;
	bool message_continued;

	// Create outgoing message (to ZMQ); lead with client ID
	if (self->outgoing_msg == NULL) {
		self->outgoing_msg = zmsg_new();
		zmsg_addstr(self->outgoing_msg, self->hashkey);
	}

	// ZWS_LOG_DEBUG(("   - Processing payload:"));
	// for (int i = 0; i < length; i++) {
	// 	ZWS_LOG_DEBUG((" %u, ", payload[i]));
	// }
	// ZWS_LOG_DEBUG(("\n"));

	// Decompress client data, if compressed
	if (self->client_compression_factor > 0) {
		// ZWS_LOG_DEBUG(("   - Decompressing client data... (compression factor %u)\n", self->client_compression_factor));
		uint8_t* outgoing_data = (uint8_t*)zmalloc(length + 4);
		bool message_continued_parsed = false;

		/* 7.2.2.  Decompression */
		memcpy(outgoing_data, payload, length);
		outgoing_data[length + 0] = 0x00;
		outgoing_data[length + 1] = 0x00;
		outgoing_data[length + 2] = 0xff;
		outgoing_data[length + 3] = 0xff;

		self->permessage_deflate_client.avail_in = length + 4;
		self->permessage_deflate_client.next_in = outgoing_data;

		// Inflate data
		do {
			uint8_t inflated_data[CHUNK] = {0};
			self->permessage_deflate_client.avail_out = CHUNK;
			self->permessage_deflate_client.next_out = inflated_data;

			int rc = inflate(&self->permessage_deflate_client, Z_NO_FLUSH);
			assert(rc != Z_STREAM_ERROR);

			switch (rc) {
				case Z_NEED_DICT:
					rc = Z_DATA_ERROR;
					break;
				case Z_DATA_ERROR:
				case Z_MEM_ERROR: {
					inflateEnd(&self->permessage_deflate_client);
					zmsg_destroy(&self->outgoing_msg);

					/* Close the client connection */
					ZWS_LOG_DEBUG(("EXCEPTION: z data / memory error (%i)\n", rc));
					self->state = CONNECTION_EXCEPTION;
					zframe_t* address = zframe_dup(self->address);
					zframe_send(&address, self->agent->stream, ZFRAME_MORE);
					zframe_t* empty = zframe_new_empty();
					zframe_send(&empty, self->agent->stream, 0);
					return;
				}
				default:
					break;
			}

			// Add inflated data to message
			unsigned int length_inflated = CHUNK - self->permessage_deflate_client.avail_out;
			if (!message_continued_parsed) {
				message_continued_parsed = true;
				message_continued = (inflated_data[0] == 1);
				zmsg_addmem(self->outgoing_msg, &inflated_data[1], length_inflated - 1);
			} else {
				zmsg_addmem(self->outgoing_msg, inflated_data, length_inflated);
			}
;
		} while (self->permessage_deflate_client.avail_out == 0);

		free(outgoing_data);

	// No decompression needed
	} else {
		// ZWS_LOG_DEBUG(("   - No decompression needed for client data, proceeding...\n"));
		message_continued = (payload[0] == 1);
		zmsg_addmem(self->outgoing_msg, &payload[1], length - 1);
	}

	// If decompression / message construction is done, send the message to the server
	if (!message_continued) {
		zmsg_send(&self->outgoing_msg, self->agent->data);
	}
}

/**
 * Send an empty ZeroMQ frame
 *
 * Used for closing ZMQ_STREAM sockets
*/
void send_close_frame(void* tag) {
	uint8_t op = 8;
	client_t* self = (client_t*)tag;
	zframe_t* address = zframe_dup(self->address);
	zframe_t* close = zframe_new(&op, 1);

	zframe_send(&address, self->agent->stream, ZFRAME_MORE);
	zframe_send(&close, self->agent->stream, 0);
	ZWS_LOG_DEBUG((" - Sent close frame to endpoint [%s] (%s)\n", self->hashkey, zsock_endpoint(self->agent->stream)));
}

/**
 * Send a websocket close frame
*/
void send_empty_frame(void* tag) {
	client_t* self = (client_t*)tag;
	zframe_t* address = zframe_dup(self->address);
	zframe_t* empty = zframe_new_empty();

	zframe_send(&address, self->agent->stream, ZFRAME_MORE);
	zframe_send(&empty, self->agent->stream, 0);
	ZWS_LOG_DEBUG((" - Sent empty frame to endpoint [%s] (%s)\n", self->hashkey, zsock_endpoint(self->agent->stream)));
}

void websocket_close(void* tag) {
	client_t* self = (client_t *)tag;
	zframe_t* address = zframe_dup(self->address);

	ZWS_LOG_DEBUG(("Closing WebSocket endpoint [%s] (%s)\n", self->hashkey, zsock_endpoint(self->agent->stream)));

	// send_close_frame(self);
	send_empty_frame(self);
}

/**
 * Callback on WebSocket "close" frame received
*/
void websocket_close_received(void* tag, byte* payload, int length) {
	client_t* self = (client_t*)tag;
	zframe_t* address = zframe_dup(self->address);

	uint16_t code = 0;
	char* reason;
	// First two bytes are a 2 byte unsigned int, code
	if (length >= 2) {
		code = payload[1] | (uint16_t)payload[0] << 8;  // Network order; big endian
		length = length - 2;
		// Remaining bytes is a UTF-8 encoded string, reason
		if (length > 2) {
			reason = malloc(sizeof(char) * (length - 2));
			memcpy(reason, payload + 2, length - 2);
		}
	}

	ZWS_LOG_DEBUG(("WebSocket close frame received from endpoint [%s] (%s)\n", self->hashkey, zsock_endpoint(self->agent->stream)));
	if (length >= 2)
		ZWS_LOG_DEBUG((" - code: %u\n", code));
	if (length > 2)
		ZWS_LOG_DEBUG((" - reason: \"%s\"\n", reason));
	
	websocket_close(tag);
}

/**
 * WebSocket "ping" frame received.
*/
void ping_received(void* tag, byte* payload, int length) {
	ZWS_LOG_DEBUG(("Ping received"));

	client_t* self = (client_t *)tag;

	byte* pong = (byte*)zmalloc(2 + length);
	pong[0] = 0x8A; // Pong and Final
	pong[1] = (byte)(length & 127);
	memcpy(pong + 2, payload, length);

	zframe_t* address = zframe_dup(self->address);
	zframe_send(&address, self->agent->stream, ZFRAME_MORE);
	zframe_t* pongf = zframe_new(pong, length + 2);
	zframe_send(&pongf, self->agent->stream, 0);
	free(pong);
}

/**
 * WebSocket "pong" frame received.
*/
void pong_received(void* tag, byte* payload, int length) {
	// TOOD: implement pong
	ZWS_LOG_DEBUG(("Pong received"));
}

/**
 *
*/
static void not_acceptable(zframe_t *_address, void* dest) {
	// ZWS_LOG_DEBUG((" - Message not acceptable\n"));
	zframe_t* address = zframe_dup(_address);
	zframe_send(&address, dest, ZFRAME_MORE + ZFRAME_REUSE);
	zstr_send (dest, "HTTP/1.1 406 Not Acceptable\r\n\r\n");

	zframe_send(&address, dest, ZFRAME_MORE);
	zframe_t* empty = zframe_new_empty();
	zframe_send(&empty, dest, 0);
}

/**
 * Read data from WebSocket endpoint client
*/
static void client_data_read(client_t* self) {
	zframe_t* data;
	zwshandshake_t* handshake;

	data = zframe_recv(self->agent->stream);

	switch (self->state) {
		case CONNECTION_CLOSED:
			// When a connection is established, a zero-length frame will be received by the application
			if (zframe_size(data) == 0) {
				ZWS_LOG_DEBUG(("Client [%s] (%s) establishing connection...\n", self->hashkey, zsock_endpoint(self->agent->stream)));
				break;
			}

			ZWS_LOG_DEBUG((" - Attempting handshake\n"));
			// TODO: We might not have received the entire request, make the zwshandshake able to handle multiple inputs
			// ZWS_LOG_DEBUG((" 	- Inflating data...\n"));
			handshake = zwshandshake_new();
			if (zwshandshake_parse_request(handshake, data)) {
				// request is valid, getting the response
				zframe_t* response = zwshandshake_get_response(handshake, &self->client_compression_factor, &self->server_compression_factor);
				if (response) {
					zframe_t* address = zframe_dup(self->address);

					zframe_send(&address, self->agent->stream, ZFRAME_MORE);
					zframe_send(&response, self->agent->stream, 0);

					free(response);

					if (self->client_compression_factor > 0) {
						// ZWS_LOG_DEBUG(("Inflating data with client compression factor %i\n", self->client_compression_factor));
						int ret = inflateInit2(&self->permessage_deflate_client, -self->client_compression_factor);
						if (ret != Z_OK) {
							ZWS_LOG_DEBUG(("EXCEPTION: Could not inflate - RC: %i\n", ret));
							ZWS_LOG_DEBUG(("	- Client compression factor: %i\n", -self->client_compression_factor));
							self->client_compression_factor = 0;
							self->state = CONNECTION_EXCEPTION;
							not_acceptable(self->address, self->agent->stream);
						}
					}
					if (self->server_compression_factor > 0) {
						int ret = deflateInit2(&self->permessage_deflate_server, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -self->server_compression_factor, 8, Z_DEFAULT_STRATEGY);
						if (ret != Z_OK) {
							ZWS_LOG_DEBUG(("EXCEPTION: Could not deflate - RC: %i\n", ret));
							ZWS_LOG_DEBUG(("	- Server compression factor: %i\n", -self->server_compression_factor));
							self->server_compression_factor = 0;
							self->state = CONNECTION_EXCEPTION;
							not_acceptable(self->address, self->agent->stream);
						}
					}

					self->decoder = zwsdecoder_new(self, &zwssock_router_message_received, &websocket_close_received, &ping_received, &pong_received);
					ZWS_LOG_DEBUG((" - Handshake successful -- client connected\n"));
					self->state = CONNECTION_CONNECTED;

				// The request is invalid
				} else {
					ZWS_LOG_DEBUG(("EXCEPTION: Invalid request: could not get response from handshake\n"));
					self->state = CONNECTION_EXCEPTION;
					not_acceptable(self->address, self->agent->stream);
				}
			} else {
				// request is invalid
				ZWS_LOG_DEBUG(("EXCEPTION: Invalid request - handshake could not be parsed\n"));
				self->state = CONNECTION_EXCEPTION;
			}
			zwshandshake_destroy(&handshake);
			break;

		case CONNECTION_CONNECTED:;
			// ZWS_LOG_DEBUG((" - Parsing message...\n"));
			if (zframe_size(data) == 0) {
				ZWS_LOG_DEBUG(("Client [%s] (%s) sent empty frame; closing connection...\n", self->hashkey, zsock_endpoint(self->agent->stream)));
				self->state = CONNECTION_EXCEPTION;
				break;
			}
			zwsdecoder_process_buffer(self->decoder, data);

			if (zwsdecoder_is_errored(self->decoder)) {
				ZWS_LOG_DEBUG(("EXCEPTION: Decoder encountered an error\n"));
				self->state = CONNECTION_EXCEPTION;
			}
			break;

		case CONNECTION_EXCEPTION:
			ZWS_LOG_DEBUG(("Client [%s] exception...\n", self->hashkey));
			// Ignore the message
			break;
	}

	zframe_destroy(&data);
}

/**
 * Callback executed when client removed from client hash table
*/
static void client_free(void* argument) {
	client_t* client = (client_t *)argument;
	zwssock_client_destroy(&client);
}

/**
 * Handle message from control socket
 *
 * Allows configuration of socket address, and termination.
*/
static int s_agent_handle_control(agent_t* self) {
	//  Get the whole message off the control socket in one go
	int rc = 0;
	zmsg_t* request = zmsg_recv(self->control);
	char* command = zmsg_popstr(request);
	if (!command)
		return -1;                  //  Interrupted

	if (streq(command, "BIND")) {
		char* endpoint = zmsg_popstr(request);
		rc = zsock_bind(self->stream, "%s", endpoint);
		assert(rc != -1);
		free(endpoint);
	}
	else if (streq(command, "UNBIND")) {
		char* endpoint = zmsg_popstr(request);
		rc = zsock_unbind(self->stream, "%s", endpoint);
		assert(rc != -1);
		free(endpoint);
	}
	else if (streq(command, "$TERM")) {
		return -1;
	}
	free(command);
	zmsg_destroy(&request);
	return rc;
}

/**
 * Handle messages from the socket
*/
static int s_agent_handle_router(agent_t* self) {
	zframe_t* address = zframe_recv(self->stream);
	char* hashkey = zframe_strhex(address);
	// ZWS_LOG_DEBUG(("Received data from endpoint [%s] (%s)\n", hashkey, zsock_endpoint(self->stream)));
	client_t* client = zhash_lookup(self->clients, hashkey);
	if (client == NULL) {
		client = zwssock_client_new(self, address);

		zhash_insert(self->clients, hashkey, client);
		zhash_freefn(self->clients, hashkey, client_free);
	}

	client_data_read(client);

	//  If client is misbehaving, remove it
	if (client->state == CONNECTION_EXCEPTION) {
		ZWS_LOG_DEBUG(("EXCEPTION: Removing client %s\n", hashkey));
		zhash_delete(self->clients, client->hashkey);
	}
	zframe_destroy(&address);
	free(hashkey);

	return 0;
}

/**
 * Compute the message header frame
*/
static void compute_frame_header(byte header, int payload_length, int* frame_size, int* payload_start_index, byte* outgoing_data) {
	*frame_size = 2 + payload_length;
	*payload_start_index = 2;

	if (payload_length > 125) {
		*frame_size += 2;
		*payload_start_index += 2;

		if (payload_length > 0xFFFF) { // 2 bytes max value
			*frame_size += 6;
			*payload_start_index += 6;
		}
	}

	outgoing_data[0] = header;

	// No mask
	outgoing_data[1] = 0x00;

	if (payload_length <= 125) {
		outgoing_data[1] |= (byte)(payload_length & 127);
	} else if (payload_length <= 0xFFFF) { // maximum size of short
		outgoing_data[1] |= 126;
		outgoing_data[2] = (payload_length >> 8) & 0xFF;
		outgoing_data[3] = payload_length & 0xFF;
	} else {
		outgoing_data[1] |= 127;
		outgoing_data[2] = 0;
		outgoing_data[3] = 0;
		outgoing_data[4] = 0;
		outgoing_data[5] = 0;
		outgoing_data[6] = (payload_length >> 24) & 0xFF;
		outgoing_data[7] = (payload_length >> 16) & 0xFF;
		outgoing_data[8] = (payload_length >> 8) & 0xFF;
		outgoing_data[9] = payload_length & 0xFF;
	}
}

/**
 * Handle outbound messages
 *
 * Sends agent data to the designated client
*/
static int s_agent_handle_data(agent_t* self) {
	// The first frame is client address (hashkey)
	// If caller provides an unknown client address, the message is ignored.
	// The assert disappears when we start to timeout clients...
	zmsg_t* request = zmsg_recv(self->data);
	char* hashkey = zmsg_popstr(request);
	client_t* client = zhash_lookup(self->clients, hashkey);

	// Unknown client
	if (!client) {
		free(hashkey);
		zmsg_destroy(&request);
		return -1;
	}

	zframe_t* address;

	// Each frame is a full ZMQ message with identity frame
	while (zmsg_size(request)) {
		zframe_t* received_frame = zmsg_pop(request);
		bool message_continued = false;

		if (zmsg_size(request))
			message_continued = true;

		if (client->server_compression_factor > 0) {
			byte byte_message_not_continued = 0;
			byte byte_message_continued = 1;

			int frame_size = zframe_size(received_frame);

			// This assumes that a compressed message is never longer than 64 bytes plus the original message. A better assumption without realloc would be great.
			unsigned int available = frame_size + 64 + 10;
			byte* compressed_payload = (byte*)zmalloc(available);
			client->permessage_deflate_server.avail_in = 1;
			client->permessage_deflate_server.next_in  = (message_continued ? &byte_message_continued : &byte_message_not_continued);
			client->permessage_deflate_server.avail_out = available;
			client->permessage_deflate_server.next_out = &compressed_payload[10];

			deflate(&client->permessage_deflate_server, Z_NO_FLUSH);

			client->permessage_deflate_server.avail_in = frame_size;
			client->permessage_deflate_server.next_in  = zframe_data(received_frame);

			deflate(&client->permessage_deflate_server, Z_SYNC_FLUSH);
			assert(client->permessage_deflate_server.avail_in == 0);

			int payload_length = available - client->permessage_deflate_server.avail_out;
			payload_length -= 4; /* skip the 0x00 0x00 0xff 0xff */

			byte initial_header[10];
			int payload_start_index;
			compute_frame_header((byte)0xC2, payload_length, &frame_size, &payload_start_index, initial_header); // 0xC2 = Final, RSV1, Binary

			// We reserved 10 extra bytes before the compressed message, here we prepend the header before it, prevents allocation of the message and a memcpy of the compressed content
			byte* outgoing_data = &compressed_payload[10 - payload_start_index];
			memcpy(outgoing_data, initial_header, payload_start_index);

			address = zframe_dup(client->address);

			zframe_send(&address, self->stream, ZFRAME_MORE);

			zframe_t* data = zframe_new(outgoing_data, frame_size);
			zframe_send(&data, self->stream, 0);

			free(compressed_payload);
			zframe_destroy(&received_frame);

		} else {
			int payload_length = zframe_size(received_frame) + 1;
			byte* outgoing_data = (byte*)zmalloc(payload_length + 10); /* + 10 = max size of header */

			int frame_size, payload_start_index;
			compute_frame_header(0x82, payload_length, &frame_size, &payload_start_index, outgoing_data); /* 0x82 = Binary and Final */

			// message_continued byte
			outgoing_data[payload_start_index] = (byte)(message_continued ? 1 : 0);
			payload_start_index++;

			// payload
			memcpy(outgoing_data + payload_start_index, zframe_data(received_frame), zframe_size(received_frame));
			address = zframe_dup(client->address);

			// ZWS_LOG_DEBUG(("Sending response to endpoint %s...\n", client->agent->stream));
			zframe_send(&address, self->stream, ZFRAME_MORE);
			zframe_t* data = zframe_new(outgoing_data, frame_size);
			zframe_send(&data, self->stream, 0);

			zframe_destroy(&received_frame);
			free(outgoing_data);
		}
		// TODO: check return code, on return code different than 0 or again set CONNECTION_EXCEPTION
	}  // End while

	free(hashkey);
	zmsg_destroy(&request);
	return 0;
}

void s_agent_task(zsock_t* control, void* args) {
	// Let the main thread continue
	zsock_signal(control, 0);

	// Create agent instance as we start this task
	agent_t* self = s_agent_new(control);
	if (!self)                  //  Interrupted
		return;

	zpoller_t* poller = zpoller_new(self->control, self->stream, self->data, NULL);
	assert(poller);

	void* which;

	while ((which = zpoller_wait(poller, -1)) != NULL) {
		if (zpoller_terminated(poller)) {
			break;

		} if (which == self->control) {
			// Something went wrong
			// TODO: use modern CZMQ patterns for handling control pipe
			if (s_agent_handle_control(self) == -1) {
				break;
			}
		} else if (which == self->stream) {
			s_agent_handle_router(self);

		} else if (which == self->data) {
			s_agent_handle_data(self);
		}
	}

	//  Done, free all agent resources
	zpoller_destroy(&poller);
	s_agent_destroy(&self);
}
