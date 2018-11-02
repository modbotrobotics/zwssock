#include <czmq.h>
#include "zwssock/zwssock.h"

static char* DEFAULT_SERVER_ADDRESS = "tcp://0.0.0.0:15798";

byte* get_next_data(zmsg_t* msg) {
  return zframe_data(zmsg_next(msg));
}

char* get_next_string(zmsg_t* msg) {
  return zframe_strdup(zmsg_next(msg));
}  // end for


int main(int argc, char** argv) {
	zwssock_t* sock;

	char* server_address =  argc > 1 ? argv[1] : DEFAULT_SERVER_ADDRESS;

	// Print ZeroMQ, CZMQ versions
	int major, minor, patch;
	zsys_version(&major, &minor, &patch);
	printf("ZeroMQ version: %d.%d.%d\nCZMQ version: %d.%d.%d\n",
		major, minor, patch,
		CZMQ_VERSION_MAJOR,
		CZMQ_VERSION_MINOR,
		CZMQ_VERSION_PATCH);

	// Create ZWSSock router
	sock = zwssock_new_router();
	int rc = zwssock_bind(sock, server_address);
	if (rc != -1) {
		printf("Router listening on \"%s\"\n", server_address);
	}
	else {
		printf("Could not bind router to address - exiting\n");
		zwssock_destroy(&sock);
		return rc;
	}

	// Respond to requests
	zmsg_t* msg;
	zframe_t* id;
	char* str_req;

	while (!zsys_interrupted) {
		// Parse request
		msg = zwssock_recv(sock);
		if (!msg)
			break;

		// The first frame is the routing ID
		id = zframe_dup(zmsg_first(msg));

		str_req = get_next_string(msg);

		printf("Received message from client [%s]: ", zframe_strdup(id));
		printf("\"%s\"", str_req);
		// printf(", %i", *(int16_t*)get_next_data(msg));
		// printf(", %i", *(int16_t*)get_next_data(msg));
		// printf(", %i", *(int32_t*)get_next_data(msg));
		// printf(", %i", *(int32_t*)get_next_data(msg));
		// printf(", %f", *(double*)get_next_data(msg));
		// printf(", %f", *(double*)get_next_data(msg));
		printf("\n");

		zmsg_destroy(&msg);


		// Create response
		zmsg_t* res = zmsg_new();

		// Add recipient client ID
		zmsg_push(res, id);

		// Add payload
		char* str_res = (char *)malloc((28 + strlen(str_req)) * sizeof(char));
		strcat(str_res, "Hello world! You sent me \'");
		strcat(str_res, str_req);
		strcat(str_res, "\'.");
		zmsg_addstr(res, str_res);

		// int16_t int_pos = 9999;
		// int16_t int_neg = -9999;
		// int32_t long_pos = 87654321;
		// int32_t long_neg = -87654321;
		// double double_pos = 87654321.12345678;
		// double double_neg = -87654321.12345678;
		// zmsg_addmem(res, &int_pos, sizeof(int_pos));
		// zmsg_addmem(res, &int_neg, sizeof(int_neg));
		// zmsg_addmem(res, &long_pos, sizeof(long_pos));
		// zmsg_addmem(res, &long_neg, sizeof(long_neg));
		// zmsg_addmem(res, &double_pos, sizeof(double_pos));
		// zmsg_addmem(res, &double_neg, sizeof(double_neg));

		int rc = zwssock_send(sock, &res);
		if (rc != 0) {
			zmsg_destroy(&res);
		}

		free(str_req);
		free(str_res);
	}

	zwssock_destroy(&sock);
	return 0;
}
