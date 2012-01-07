#include "socket_read_buffers.h"
#include "reallocf.h"

#define READ_BUFFER_ALLOCATION_INCREMENT 8192

char *next_line_from_read_buffer(int socket, struct ReadBuffer *read_buffer) {
	ssize_t result;
	size_t line_position = read_buffer->buf_position;

	while (1) {
		// if there's no unconsumed data in the buffer, read some more into it
		if (read_buffer->buf_position == read_buffer->buf_data_length) {
			// if there are no bytes free in the buffer, enlarge it by READ_BUFFER_ALLOCATION_INCREMENT
			if (read_buffer->buf_allocated_size == read_buffer->buf_data_length) {
				read_buffer->buf_allocated_size += READ_BUFFER_ALLOCATION_INCREMENT;
				read_buffer->buf = reallocf(read_buffer->buf, read_buffer->buf_allocated_size);
			}

			// if we were unable to allocate memory, return and let the caller print an error based on errno
			if (!read_buffer->buf) return NULL;

			// read some bytes into the buffer
			result = recv(socket, read_buffer->buf + read_buffer->buf_data_length, read_buffer->buf_allocated_size - read_buffer->buf_data_length, 0);
			if (result > 0) {
				// the buffer now has the returned amount of data more
				read_buffer->buf_data_length += result;
			} else if (result == 0) {
				// the other side has closed its half of the connection and we haven't found a newline
				// we choose to return the line so far (which may be an empty string), as most IO libraries do
				read_buffer->buf_position = read_buffer->buf_data_length;
				return read_buffer->buf + line_position;
			} else if (errno != EINTR) {
				// return and let the caller print an error based on errno
				return NULL;
			}
		}

		// there is at least one byte available in the buffer; if the next byte is a newline, we've reached the end of the line
		if (*(read_buffer->buf + read_buffer->buf_position) == '\n') {
			// turn the \n into a \0 so it terminates the line string
			*(read_buffer->buf + read_buffer->buf_position) = 0;

			// furthermore, if the previous character on the line was a \r (which it should be for an internet protocol), turn that into a \0 so it terminates the line string
			if (read_buffer->buf_position > line_position &&
				*(read_buffer->buf + read_buffer->buf_position - 1) == '\r') {
				*(read_buffer->buf + read_buffer->buf_position - 1) = 0;
			}

			// the next call should start from the byte after our newline (which may be the byte after the end of the buffer, indicating it's consumed)
			read_buffer->buf_position += 1;

			// return the line start, which is now a NUL-terminated string
			return read_buffer->buf + line_position;
		}

		// move on to the next byte
		read_buffer->buf_position += 1;
	}
}

int read_and_discard_from_read_buffer(int socket, struct ReadBuffer *read_buffer, size_t bytes_to_discard) {
	char discard[2048];
	ssize_t result;

	// first, consume any available bytes in the read buffer
	bytes_to_discard -= (read_buffer->buf_data_length - read_buffer->buf_position);
	read_buffer->buf_position = read_buffer->buf_data_length;

	// then read from the socket
	while (bytes_to_discard > 0) {
		result = recv(socket, &discard, (bytes_to_discard > sizeof(discard) ? sizeof(discard) : bytes_to_discard), 0);
		if (result > 0) {
			bytes_to_discard -= result;
		} else if (result == 0) {
			errno = 0;
			return -1;
		} else if (errno != EINTR) {
			return -1;
		}
	}

	return 0;
}
