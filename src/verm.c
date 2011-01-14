/**
 * This file is a part of verm
 * Copyright (c) Will Bryant, Sekuda Limited 2011
 */

#define HTTP_PORT 1138
#define HTTP_TIMEOUT 60
#define EXTRA_DAEMON_FLAGS MHD_USE_DEBUG

#define ROOT "/var/lib/verm"

#define HTTP_404_PAGE "<!DOCTYPE html><html><head><title>Verm - File not found</title></head><body>File not found</body></html>"

#include "platform.h"
#include "microhttpd.h"

int send_file_not_found_response(struct MHD_Connection* connection) {
	struct MHD_Response* response;
	int ret;
	
	response = MHD_create_response_from_buffer(strlen(HTTP_404_PAGE), HTTP_404_PAGE, MHD_RESPMEM_PERSISTENT);
	ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response); // cleanly returns MHD_NO if response was NULL for any reason
	MHD_destroy_response(response); // does nothing if response was NULL for any reason
	return ret;
}

int handle_get_request(
	void* _daemon_data, struct MHD_Connection* connection,
    const char* path, void** _request_data) {

	int fd;
	struct stat st;
	struct MHD_Response* response;
	int ret;
	char fs_path[256];

	// check and expand the path (although the MHD docs use 'url' as the name for this parameter, it's actually the path - it does not include the scheme/hostname/query, and has been URL-decoded)
	if (path[0] != '/' || strstr(path, "/..") ||
	    snprintf(fs_path, sizeof(fs_path), "%s%s", ROOT, path) >= sizeof(fs_path)) {
		return send_file_not_found_response(connection);
	}
	
	fprintf(stderr, "opening %s\n", fs_path);
	fd = open(fs_path, O_RDONLY);
	if (fd < 0) {
		switch (errno) {
			case ENOENT:
			case EACCES:
				return send_file_not_found_response(connection);
			
			default:
				fprintf(stderr, "Failed to open %s: %s (%d)\n", fs_path, strerror(errno), errno);
				return MHD_NO;
		}
	}
	
	if (fstat(fd, &st) < 0) { // should never happen
		fprintf(stderr, "Couldn't fstat open file %s!\n", fs_path);
		close(fd);
		return MHD_NO;
	}
	
	// FUTURE: support range requests
	// TODO: add caching headers
	response = MHD_create_response_from_fd_at_offset(st.st_size, fd, 0); // fd will be closed by MHD when the response is destroyed
	if (!response) { // presumably out of memory
		fprintf(stderr, "Couldn't create response from file %s (out of memory?)\n", fs_path);
		close(fd);
	}
	ret = MHD_queue_response(connection, MHD_HTTP_OK, response); // does nothing and returns our desired MHD_NO if response is NULL
	MHD_destroy_response(response); // does nothing if response is NULL
	return ret;
}

int handle_request(
	void* _daemon_data, struct MHD_Connection* connection,
    const char* path, const char* method, const char* version,
    const char* upload_data, size_t* upload_data_size,
	void** request_data) {
	
	if (strcmp(method, "GET") == 0) {
		return handle_get_request(_daemon_data, connection, path, request_data);
		
	} else {
		return MHD_NO;
	}
}

int main(int argc, const char* argv[]) {
	struct MHD_Daemon* daemon;
	
	daemon = MHD_start_daemon(
		MHD_USE_THREAD_PER_CONNECTION | EXTRA_DAEMON_FLAGS,
		HTTP_PORT,
		NULL, NULL, // no connection address check
		&handle_request, NULL, // no extra argument to handle_request
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) HTTP_TIMEOUT,
		MHD_OPTION_END);
	
	if (daemon == NULL) {
		fprintf(stderr, "couldn't start daemon");
		return 1;
	}
	
	// TODO: write a proper daemon loop
	fprintf(stdout, "Verm listening on http://localhost:%d/\n", HTTP_PORT);
	(void) getc (stdin);

	MHD_stop_daemon(daemon);
	return 0;
}
