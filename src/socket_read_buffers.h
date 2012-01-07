#ifndef SOCKET_READ_BUFFERS_H
#define SOCKET_READ_BUFFERS_H

#include "platform.h"

struct ReadBuffer {
	char *buf;
	size_t buf_allocated_size;
	size_t buf_data_length;
	size_t buf_position;
};
#define EMPTY_READ_BUFFER {NULL, 0, 0, 0}

char *next_line_from_read_buffer(int socket, struct ReadBuffer *read_buffer);
int read_and_discard_from_read_buffer(int socket, struct ReadBuffer *read_buffer, size_t bytes_to_discard);

#endif
