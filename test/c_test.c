#include <czmq.h>
#include "zwssock/zwssock.h"

static char* DEFAULT_SERVER_ADDRESS = "tcp://0.0.0.0:15798";


int main(int argc, char** argv) {
	zwssock_t* sock;

	char* server_address =  argc > 1 ? argv[1] : DEFAULT_SERVER_ADDRESS;

	// Print ZeroMQ, CZMQ versions
	int major, minor, patch;
	zsys_version (&major, &minor, &patch);
	printf("ZeroMQ version: %d.%d.%d\nCZMQ version: %d.%d.%d\n",
		major, minor, patch,
		CZMQ_VERSION_MAJOR, CZMQ_VERSION_MINOR,CZMQ_VERSION_PATCH);

	// Create ZWSSock router
	sock = zwssock_new_router();
	zwssock_bind(sock, server_address);

	// Respond to requests
	zmsg_t* msg;
	zframe_t* id;

	while (!zsys_interrupted) {
		// Parse request
		msg = zwssock_recv(sock);
		if (!msg)
			break;

		// The first frame is the routing ID
		id = zmsg_pop(msg);

		char* str = zmsg_popstr(msg);
		printf("Received: \"%s\"\n", str);

		free(str);
		zmsg_destroy(&msg);

		// Create response
		msg = zmsg_new();
		zmsg_push(msg, id);
		zmsg_addstr(msg, "Hello world!");

		int rc = zwssock_send(sock, &msg);
		if (rc != 0) {
			zmsg_destroy(&msg);
		}
	}

	zwssock_destroy(&sock);
}
