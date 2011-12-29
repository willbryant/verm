#include "settings.h"
#include "replication.h"
#include "settings.h"
#include "str.h"

#define BACKOFF_BASE_TIME 1
#define BACKOFF_MAX_TIME 60
#define MAX_PATH_LENGTH 512

#define HTTP_CREATED_STATUS 201
#define HTTP_CONTENT_LENGTH "Content-Length:"

struct Replicator {
	char* hostname;
	char* service;

	pthread_t thread;
	int need_resync;
	struct ReplicationFile *next_file;
	int failed_push_attempts;
	int socket;

	struct Replicator *next_replicator;
};

struct ReplicationFile {
	char location[MAX_PATH_LENGTH];
	char path[MAX_PATH_LENGTH];
	const char* encoding;
	time_t queued_at;

	struct ReplicationFile *next_file;
};

static pthread_mutex_t replication_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  replication_queue_cond  = PTHREAD_COND_INITIALIZER;
static int replication_shutdown = 0;
static int number_of_replicators = 0;
static struct Replicator *last_replicator = NULL;

void replication_backoff(struct Replicator *replicator) {
	struct timeval tv;
	struct timespec ts;

	// we use binary exponential backoff times, but after 1 failed push attempt we want to retry immediately, and
	// only after 2 failed push attempts wait the base backoff time, and thereafter double that time after each failure
	int backoff_time = BACKOFF_BASE_TIME*(2 << replicator->failed_push_attempts - 3);
	if (backoff_time > BACKOFF_MAX_TIME) backoff_time = BACKOFF_MAX_TIME;

	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec + backoff_time;
	ts.tv_nsec = 0;

	while (!replication_shutdown && !pthread_cond_timedwait(&replication_queue_cond, &replication_queue_mutex, &ts)) ;
}

void replication_free_queue(struct Replicator *replicator) {
	struct ReplicationFile *file;
	while (file = replicator->next_file) {
		replicator->next_file = file->next_file;
		free(file);
	}
}

void replication_close_connection(struct Replicator *replicator) {
	if (replicator->socket) {
		close(replicator->socket);
		replicator->socket = 0;
	}
}

void replication_open_connection(struct Replicator *replicator) {
	struct addrinfo hints;
	struct addrinfo *addr, *addr0 = NULL;
	int error;
	char* last_error = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG;
	error = getaddrinfo(replicator->hostname, replicator->service, &hints, &addr0);
	if (error) {
		fprintf(stderr, "Couldn't resolve %s:%s: %s\n", replicator->hostname, replicator->service, gai_strerror(error));
		return;
	}

	for (addr = addr0; addr; addr = addr->ai_next) {
		replicator->socket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (!replicator->socket) {
			last_error = "Couldn't open a socket to connect to %s:%s: %s\n";
			// loop around and retry
		} else if (connect(replicator->socket, addr->ai_addr, addr->ai_addrlen) < 0) {
			last_error = "Couldn't connect to %s:%s: %s\n";
			replication_close_connection(replicator);
			// loop around and retry
		} else {
			DEBUG_PRINT("connected to %s:%s\n", replicator->hostname, replicator->service);
			break;
		}
	}

	if (!replicator->socket) {
		fprintf(stderr, last_error, replicator->hostname, replicator->service, strerror(errno));
	}

	freeaddrinfo(addr0);
}

void replication_send_data(struct Replicator *replicator, const char *data, int len) {
	int result;
	while (!replication_shutdown && replicator->socket && len > 0) {
		result = send(replicator->socket, data, len, 0);
		if (result > 0) {
			len -= result;
			data += result;
		} else if (errno != EINTR) {
			fprintf(stderr, "Couldn't write to %s:s: %s\n", replicator->hostname, replicator->service, strerror(errno));
			replication_close_connection(replicator);
		}
	}
}

// defined as a macro so strlen can be precalculated by the compiler where str is a fixed string
#define replication_send_string(replicator, str) replication_send_data(replicator, str, strlen(str))

void replication_send_integer(struct Replicator *replicator, intmax_t i) {
	char* buf;
	int result = asprintf(&buf, "%jd", i);
	if (buf) {
		replication_send_data(replicator, buf, result);
		free(buf);
	} else {
		perror("Couldn't allocate a buffer");
		replication_close_connection(replicator);
	}
}

void replication_send_http_request_header(struct Replicator *replicator, const char *method, const char *location, const char *encoding, size_t content_length) {
	replication_send_string(replicator, method);
	replication_send_string(replicator, " ");
	replication_send_string(replicator, location);
	replication_send_string(replicator, " ");
	replication_send_string(replicator, "HTTP/1.0\r\n");

	replication_send_string(replicator, "Host: ");
	replication_send_string(replicator, replicator->hostname);
	replication_send_string(replicator, "\r\n");

	if (encoding) {
		replication_send_string(replicator, "Content-Encoding: ");
		replication_send_string(replicator, encoding);
		replication_send_string(replicator, "\r\n");
	}

	replication_send_string(replicator, "Content-Length: ");
	replication_send_integer(replicator, content_length);
	replication_send_string(replicator, "\r\n\r\n");
}

void replication_send_file(struct Replicator *replicator, const char *path, size_t file_length, int fd) {
	char buf[8192]; // arbitrary
	ssize_t bread, bwritten;

	off_t offset = 0;
	while (1) {
		if (offset == file_length) return; // completed successfully, exit without closing the connection

		bread = pread(fd, &buf, sizeof(buf), offset);
		if (bread > 0) {
			bwritten = send(replicator->socket, &buf, bread, 0);
			if (bwritten > 0) {
				// we have successfully written some bytes to the socket.  because our buffer is smaller than the SO_SNDBUF (on all supported
				// operating systems - indeed all current OSs use 8k or more, eg. Linux uses 128k - so we don't need to request this ourselves)
				// and send is a blocking call (it will wait until space becomes available in the buffer if it fills it) usually all the bytes
				// we supplied will have been written.  but if our write was interrupted by a signal, it's possible that we have written fewer
				// than we read - bwritten <= bread.  but that's ok - using pread and specifying the offset, which we increment only bwritten,
				// means that we will correctly start the next iteration from after the data written, and it's rare enough for there to be no
				// point adding extra code to handle partial writes, especially since the re-read will in reality be cached.
				offset += bwritten;
			} else if (errno != EINTR) {
				fprintf(stderr, "Error writing to %s:%s: %s (%d)\n", replicator->hostname, replicator->service, strerror(errno), errno);
				break;
			}
		} else if (bread == 0) {
			// no error condition was returned so this means the offset was at or past the end of the file, which doesn't match our counter and therefore the info fstat returned after we opened the file, so presumably the file has been truncated
			fprintf(stderr, "Error reading from %s: file truncated?\n", path);
			break;
		} else if (errno != EINTR) {
			fprintf(stderr, "Error reading from %s: %s (%d)\n", path, strerror(errno), errno);
			break;
		}
	}

	// error encountered, close the connection
	replication_close_connection(replicator);
}

#define READ_BUFFER_ALLOCATION_INCREMENT 8192
struct ReadBuffer {
	char *buf;
	size_t buf_allocated_size;
	size_t buf_data_length;
	size_t buf_position;
};

char *next_line_from_read_buffer(int socket, struct ReadBuffer *read_buffer) {
	ssize_t result;
	size_t line_position = read_buffer->buf_position;

	while (1) {
		// if there's no unconsumed data in the buffer, read some more into it
		if (read_buffer->buf_position == read_buffer->buf_data_length) {
			// if there are no bytes free in the buffer, enlarge it by READ_BUFFER_ALLOCATION_INCREMENT
			if (read_buffer->buf_allocated_size == read_buffer->buf_data_length) {
				read_buffer->buf_allocated_size += READ_BUFFER_ALLOCATION_INCREMENT;
				read_buffer->buf = realloc(read_buffer->buf, read_buffer->buf_allocated_size);
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

			// return the line start, which is now a null-terminated string
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

int replication_check_response(struct Replicator *replicator, const char *location, int expected_response_code) {
	struct ReadBuffer read_buffer = {NULL, 0, 0, 0};
	char *line, *p;
	int response_code = 0, successful = 0;
	long content_length = -1;

	if (!replicator->socket) return 0;

	// read the status line
	line = next_line_from_read_buffer(replicator->socket, &read_buffer);

	// parse the status line
	if (line && strncmp(line, "HTTP/", 5) == 0) {
		p = line;
		while (*p && *p++ != ' ') ;
		if (*p) response_code = atoi(p);
		successful = (response_code == expected_response_code);
	}

	if (!successful) {
		if (response_code > 0 && response_code < 600) {
			fprintf(stderr, "Replication HTTP request to %s:%s for %s failed: %s", replicator->hostname, replicator->service, location, p);
		} else if (line) {
			fprintf(stderr, "Replication HTTP request to %s:%s for %s returned an invalid HTTP response: %s\n", replicator->hostname, replicator->service, location, line);
		} else {
			fprintf(stderr, "Replication HTTP request to %s:%s for %s failed: %s (%d)", replicator->hostname, replicator->service, location, strerror(errno), errno);
		}
	} else {
		// read lines of HTTP response header until we get to the blank line that separates it from the response body (if any)
		while ((line = next_line_from_read_buffer(replicator->socket, &read_buffer)) && *line) {
			if (strncmp(line, HTTP_CONTENT_LENGTH, strlen(HTTP_CONTENT_LENGTH)) == 0) {
				p = line + strlen(HTTP_CONTENT_LENGTH);
				while (*p == ' ') p++;
				content_length = atoi_or_default(p, -1);
				if (content_length < 0) {
					fprintf(stderr, "Replication HTTP request to %s:%s for %s returned an invalid content-length header response: %s\n", replicator->hostname, replicator->service, location, line);
					successful = 0;
				}
			}
		}

		if (!line) {
			fprintf(stderr, "Replication HTTP request to %s:%s for %s failed while reading response header: %s (%d)", replicator->hostname, replicator->service, location, strerror(errno), errno);
			successful = 0;
		} else if (content_length >= 0) {
			if (read_and_discard_from_read_buffer(replicator->socket, &read_buffer, content_length) < 0) {
				if (errno) {
					fprintf(stderr, "Replication HTTP request to %s:%s for %s failed while reading response body: %s (%d)", replicator->hostname, replicator->service, location, strerror(errno), errno);
				} else {
					fprintf(stderr, "Replication HTTP request to %s:%s for %s failed: connection closed while reading response body", replicator->hostname, replicator->service, location);
				}
				successful = 0;
			}
		} else {
			// the request was successful, but we didn't receive a content-length header back and therefore can't reuse the connection, so close it now
			replication_close_connection(replicator);
		}
	}

	free(read_buffer.buf);
	if (!successful) replication_close_connection(replicator);
	return successful;
}

int replication_check_put_response(struct Replicator *replicator, const char *location) {
	return replication_check_response(replicator, location, HTTP_CREATED_STATUS);
}

int push_file(struct Replicator *replicator, struct ReplicationFile *file) {
	DEBUG_PRINT("replicating %s to %s:%s\n", file->location, replicator->hostname, replicator->service);

	int fd = open(file->path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Couldn't open %s: %s (%d)\n", file->path, strerror(errno), errno);
		replicator->need_resync = 1; // so we flush the queue and scan for files, so we don't get stuck on a disappeared file
		return 0;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "Couldn't stat %s: %s (%d)\n", file->path, strerror(errno), errno);
		return 0;
	}

	if (!replicator->socket) replication_open_connection(replicator);
	if (replicator->socket) replication_send_http_request_header(replicator, "PUT", file->location, file->encoding, st.st_size);
	if (replicator->socket) replication_send_file(replicator, file->path, st.st_size, fd);
	close(fd);

	int successful = replication_check_put_response(replicator, file->location);
	if (successful) {
		replicator->failed_push_attempts = 0;
	} else {
		replicator->failed_push_attempts++;
	}
	log_replication_statistic(successful);
	return successful;
}

int resync(struct Replicator *replicator) {
	DEBUG_PRINT("resyncing to %s:%s\n", replicator->hostname, replicator->service);
	// TODO: implement directory scanning and call push_file
	return 0;
}

void *replication_thread_entry(void *data) {
	int successful;
	struct Replicator *replicator = (struct Replicator *)data;
	struct ReplicationFile *file = NULL;

	DEBUG_PRINT("replicating to %s:%s\n", replicator->hostname, replicator->service);
	if (pthread_mutex_lock(&replication_queue_mutex) < 0) return NULL;

	while (!replication_shutdown) {
		if (replicator->need_resync) {
			replication_free_queue(replicator);
			replicator->need_resync = 0;

			if (pthread_mutex_unlock(&replication_queue_mutex) < 0) return NULL;
			successful = resync(replicator);
			if (pthread_mutex_lock(&replication_queue_mutex) < 0) return NULL;

			if (!successful) {
				replicator->need_resync = 1;
				replication_close_connection(replicator);
				replication_backoff(replicator);
			}

		} else if (replicator->next_file) {
			file = replicator->next_file;
			if (pthread_mutex_unlock(&replication_queue_mutex) < 0) return NULL;
			successful = push_file(replicator, file);
			if (pthread_mutex_lock(&replication_queue_mutex) < 0) return NULL;

			if (successful) {
				// move on to the next file
				replicator->next_file = file->next_file;
				free(file);
			} else {
				replication_close_connection(replicator);
				replication_backoff(replicator);
			}

		} else {
			replication_close_connection(replicator);
			if (pthread_cond_wait(&replication_queue_cond, &replication_queue_mutex) < 0) return NULL;
			continue;
		}
	}

	replication_close_connection(replicator);
	replication_free_queue(replicator);

	if (pthread_mutex_unlock(&replication_queue_mutex) < 0) return NULL;
	return NULL;
}

int add_replication_target(char *hostname, char *service) {
	struct Replicator* replicator = (struct Replicator *)malloc(sizeof(struct Replicator));
	if (!replicator) return -1;

	replicator->hostname = strdup(hostname);
	if (!replicator->hostname) {
		free(replicator);
		return -1;
	}
	replicator->service = service;
	replicator->next_file = NULL;
	replicator->next_replicator = last_replicator;
	replicator->failed_push_attempts = 0;
	replicator->socket = 0;
	if (pthread_create(&replicator->thread, NULL, &replication_thread_entry, replicator) < 0) return -1;

	last_replicator = replicator;
	number_of_replicators++;
	return 0;
}

int parse_and_add_replication_target(char *target) {
	int ret;
	char *colon = strchr(target, ':');
	if (colon) {
		*colon = 0;
		ret = add_replication_target(target, colon + 1);
		*colon = ':';
		return ret;
	} else {
		return add_replication_target(target, DEFAULT_HTTP_PORT);
	}
}

void add_replication_file(const char *location, const char *path, const char *encoding) {
	struct Replicator *replicator;
	pthread_mutex_lock(&replication_queue_mutex);

	for (replicator = last_replicator; replicator != NULL; replicator = replicator->next_replicator) {
		if (replicator->need_resync) continue;

		struct ReplicationFile *file = malloc(sizeof(struct ReplicationFile));
		if (file) {
			strncpy(file->location, location, sizeof(file->location));
			strncpy(file->path, path, sizeof(file->path));
			file->encoding = encoding; // static strings
			time(&file->queued_at);
			file->next_file = NULL;

			if (replicator->next_file) {
				replicator->next_file->next_file = file;
			} else {
				replicator->next_file = file;
			}
		} else {
			// since we haven't added the file to the queue for this replicator, set the flag asking for a complete resync
			// nb. we can't free its queue at this point because although we hold the queue mutex, the replication thread
			// may be using one of the records from its queue without holding the mutex - which is only for mutating the queue
			replicator->need_resync = 1;
		}
	}

	pthread_cond_broadcast(&replication_queue_cond);
	pthread_mutex_unlock(&replication_queue_mutex);
}

void shutdown_replication() {
	struct Replicator *replicator, *next;
	
	pthread_mutex_lock(&replication_queue_mutex);
	replication_shutdown = 1;
	pthread_cond_broadcast(&replication_queue_cond);
	pthread_mutex_unlock(&replication_queue_mutex);

	replicator = last_replicator;
	while (replicator) {
		next = replicator->next_replicator;

		pthread_join(replicator->thread, NULL);
		free(replicator->hostname);
		free(replicator);

		replicator = next;
	}
}
