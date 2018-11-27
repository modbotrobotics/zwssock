#include "zwsdecoder.h"

typedef enum {
	opcode_continuation		= 0,
	opcode_text 					= 0x01,
	opcode_binary 				= 0x02,
	opcode_close 					= 0x08,
	opcode_ping 					= 0x09,
	opcode_pong 					= 0xA
} opcode_t;

typedef enum {
	STATE_NEW_MESSAGE,
	STATE_SECOND_BYTE,
	STATE_SHORT_SIZE,
	STATE_SHORT_SIZE_2,

	STATE_LONG_SIZE,
	STATE_LONG_SIZE_2,
	STATE_LONG_SIZE_3,
	STATE_LONG_SIZE_4,
	STATE_LONG_SIZE_5,
	STATE_LONG_SIZE_6,
	STATE_LONG_SIZE_7,
	STATE_LONG_SIZE_8,

	STATE_MASK,
	STATE_MASK_2,
	STATE_MASK_3,
	STATE_MASK_4,

	STATE_BEGIN_PAYLOAD,
	STATE_PAYLOAD,
	STATE_ERROR
} state_t;

struct _zwsdecoder_t {
	state_t state;
	opcode_t opcode;
	bool is_masked;
	byte mask[4];
	byte* payload;
	int payload_length;
	int payload_index;
	void* tag;
	message_callback_t message_cb;
	close_callback_t close_cb;
	ping_callback_t ping_cb;
	pong_callback_t pong_cb;
};

// Private methods
static void invoke_new_message(zwsdecoder_t* self);
static state_t zwsdecoder_next_state(zwsdecoder_t* self);
static void zwsdecoder_process_byte(zwsdecoder_t* self, byte b);

zwsdecoder_t* zwsdecoder_new(
		void* tag,
		message_callback_t message_cb,
		close_callback_t close_cb,
		ping_callback_t ping_cb,
		pong_callback_t pong_cb) {
	zwsdecoder_t* self = zmalloc(sizeof(zwsdecoder_t));

	self->state = STATE_NEW_MESSAGE;
	self->tag = tag;
	self->message_cb = message_cb;
	self->close_cb = close_cb;
	self->ping_cb = ping_cb;
	self->pong_cb = pong_cb;
	self->payload = NULL;

	return self;
}

void zwsdecoder_destroy(zwsdecoder_t** self_p) {
	zwsdecoder_t* self = *self_p;
	free(self);
	*self_p = NULL;
}

void zwsdecoder_process_buffer(zwsdecoder_t* self, zframe_t* data) {
	int i = 0;

	byte* buffer = zframe_data(data);
	int buffer_length = zframe_size(data);

	// printf("   - Processing buffer:");
	// for (int i = 0; i < buffer_length; i++) {
	// 	printf(" %u, ", buffer[i]);
	// }
	// printf("\n");

	int bytes_to_read;

	while (i < buffer_length) {
		switch (self->state) {
			case STATE_ERROR:
				return;

			// Set-up payload
			case STATE_BEGIN_PAYLOAD:
				self->payload_index = 0;
				self->payload = zmalloc(sizeof(byte) * (self->payload_length + 4)); // +4 extra bytes in case we have to inflate it


			case STATE_PAYLOAD:
				bytes_to_read = self->payload_length - self->payload_index;

				if (bytes_to_read > (buffer_length - i)) {
					bytes_to_read = buffer_length - i;
				}

				memcpy(self->payload + self->payload_index, buffer + i, bytes_to_read);

				// If masked, apply the mask
				if (self->is_masked) {
					for (int j = self->payload_index; j < self->payload_index + bytes_to_read; j++) {
						self->payload[j] = self->payload[j] ^ self->mask[(j) % 4];
					}
				}

				self->payload_index += bytes_to_read;
				i += bytes_to_read;

				// If we are processing the payload, continue to payload state
				if (self->payload_index < self->payload_length) {
					self->state = STATE_PAYLOAD;

				// If we have passed the payload, we are processing a new message
				} else {
					self->state = STATE_NEW_MESSAGE;
					invoke_new_message(self);
				}
				break;

			default:
				zwsdecoder_process_byte(self, buffer[i]);
				i++;
				break;
		}
	}
}

static void zwsdecoder_process_byte(zwsdecoder_t* self, byte b) {
	bool final;

	switch (self->state) {
		case STATE_NEW_MESSAGE:
			final = (b & 0x80) != 0; // final bit
			self->opcode = b & 0xF; // opcode bit


			// not final messages are currently not supported
			if (!final) {
				// printf("Decoder: unsupported FINAL\n");
				self->state = STATE_ERROR;

			// Check that the opcode is supported
			} else if (self->opcode != opcode_binary
						&& self->opcode != opcode_close
						&& self->opcode != opcode_ping
						&& self->opcode != opcode_pong) {
				// printf("Decoder: unsupported OP code\n");
				self->state = STATE_ERROR;
			} else {
				self->state = STATE_SECOND_BYTE;
			}
			break;

		case STATE_SECOND_BYTE:
			self->is_masked = (b & 0x80) != 0;
			byte length = (byte)(b & 0x7F);

			if (length < 126) {
				self->payload_length = length;
				self->state = zwsdecoder_next_state(self);

			} else if (length == 126) {
				self->state = STATE_SHORT_SIZE;

			} else {
				self->state = STATE_LONG_SIZE;
			}
			break;

		case STATE_MASK:
			self->mask[0] = b;
			self->state = STATE_MASK_2;
			break;

		case STATE_MASK_2:
			self->mask[1] = b;
			self->state = STATE_MASK_3;
			break;

		case STATE_MASK_3:
			self->mask[2] = b;
			self->state = STATE_MASK_4;
			break;

		case STATE_MASK_4:
			self->mask[3] = b;
			self->state = zwsdecoder_next_state(self);
			break;

		case STATE_SHORT_SIZE:
			self->payload_length = b << 8;
			self->state = STATE_SHORT_SIZE_2;
			break;

		case STATE_SHORT_SIZE_2:
			self->payload_length |= b;
			self->state = zwsdecoder_next_state(self);
			break;

		case STATE_LONG_SIZE:
			self->payload_length = 0;

			// must be zero, max message size is MaxInt
			if (b != 0) {
				// printf("Decoder: message size exceeded max (MaxInt)\n");
				self->state = STATE_ERROR;
			}
			else
				self->state = STATE_LONG_SIZE_2;

			break;

		case STATE_LONG_SIZE_2:
			// must be zero, max message size is MaxInt
			if (b != 0) {
				// printf("Decoder: message size exceeded max (MaxInt)\n");
				self->state = STATE_ERROR;
			}
			else
				self->state = STATE_LONG_SIZE_3;
			break;

		case STATE_LONG_SIZE_3:
			// must be zero, max message size is MaxInt
			if (b != 0) {
				// printf("Decoder: message size exceeded max (MaxInt)\n");
				self->state = STATE_ERROR;
			}
			else
				self->state = STATE_LONG_SIZE_4;
			break;

		case STATE_LONG_SIZE_4:
			// must be zero, max message size is MaxInt
			if (b != 0) {
				// printf("Decoder: message size exceeded max (MaxInt)\n");
				self->state = STATE_ERROR;
			}
			else
				self->state = STATE_LONG_SIZE_5;
			break;

		case STATE_LONG_SIZE_5:
			self->payload_length |= b << 24;
			self->state = STATE_LONG_SIZE_6;
			break;

		case STATE_LONG_SIZE_6:
			self->payload_length |= b << 16;
			self->state = STATE_LONG_SIZE_7;
			break;

		case STATE_LONG_SIZE_7:
			self->payload_length |= b << 8;
			self->state = STATE_LONG_SIZE_8;
			break;

		case STATE_LONG_SIZE_8:
			self->payload_length |= b;
			self->state = zwsdecoder_next_state(self);
			break;

		case STATE_BEGIN_PAYLOAD:
			break;

		case STATE_PAYLOAD:
			break;

		case STATE_ERROR:
			// printf("Decoder: error encountered...\n");
			// UNIMPLEMENTED
			break;
	}
}

static state_t zwsdecoder_next_state(zwsdecoder_t* self) {
	if ((self->state == STATE_LONG_SIZE_8 || self->state == STATE_SECOND_BYTE || self->state == STATE_SHORT_SIZE_2) && self->is_masked) {
		return STATE_MASK;
	}
	else {
		if (self->payload_length == 0) {
			invoke_new_message(self);

			return STATE_NEW_MESSAGE;
		}
		else
			return STATE_BEGIN_PAYLOAD;

	}
}

static void invoke_new_message(zwsdecoder_t* self) {
	switch (self->opcode) {
		case opcode_binary:
			self->message_cb(self->tag, self->payload, self->payload_length);
			break;
		case opcode_close:
			self->close_cb(self->tag, self->payload, self->payload_length);
			break;
		case opcode_ping:
			self->ping_cb(self->tag, self->payload, self->payload_length);
			break;
		case opcode_pong:
			self->pong_cb(self->tag, self->payload, self->payload_length);
			break;
		default:
			assert(false);
	}

	if (self->payload != NULL) {
		free(self->payload);
		self->payload = NULL;
	}
}

bool zwsdecoder_is_errored(zwsdecoder_t* self) {
	return self->state == STATE_ERROR;
}
