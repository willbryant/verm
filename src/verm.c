/**
 * This file is a part of verm
 * Copyright (c) Will Bryant, Sekuda Limited 2011
 */

#define HTTP_PORT 1138
#define HTTP_TIMEOUT 60
#define POST_BUFFER_SIZE 65536
#define EXTRA_DAEMON_FLAGS MHD_USE_DEBUG

#define ROOT "/var/lib/verm"

#define HTTP_404_PAGE "<!DOCTYPE html><html><head><title>Verm - File not found</title></head><body>File not found</body></html>"
#define UPLOAD_PAGE "<!DOCTYPE html><html><head><title>Verm - Upload</title></head><body>" \
                    "<form method='post' enctype='multipart/form-data'>" \
                    "<input name='uploaded_file' type='file'><input type='submit' value='Upload'>" \
                    "</form>" \
                    "</body></html>"
#define REDIRECT_PAGE "You are being redirected"

#include "platform.h"
#include "microhttpd.h"

struct Upload {
	char tempfile_fs_path[256];
	int tempfile_fd;
	struct MHD_PostProcessor* pp;
};

int send_static_page_response(struct MHD_Connection* connection, unsigned int status_code, char* page) {
	struct MHD_Response* response;
	int ret;
	
	response = MHD_create_response_from_buffer(strlen(page), page, MHD_RESPMEM_PERSISTENT);
	ret = MHD_queue_response(connection, status_code, response); // cleanly returns MHD_NO if response was NULL for any reason
	MHD_destroy_response(response); // does nothing if response was NULL for any reason
	return ret;
}

int send_file_not_found_response(struct MHD_Connection* connection) {
	return send_static_page_response(connection, MHD_HTTP_NOT_FOUND, HTTP_404_PAGE);
}

int send_redirect(struct MHD_Connection* connection, unsigned int status_code, char* location) {
	struct MHD_Response* response;
	int ret;
	
	response = MHD_create_response_from_buffer(strlen(REDIRECT_PAGE), REDIRECT_PAGE, MHD_RESPMEM_PERSISTENT);
	ret = MHD_add_response_header(response, "Location", location) &&
	      MHD_queue_response(connection, status_code, response); // cleanly returns MHD_NO if response was NULL for any reason
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

	if (strcmp(path, "/") == 0) {
		return send_static_page_response(connection, MHD_HTTP_OK, UPLOAD_PAGE);
	}
	
	// check and expand the path (although the MHD docs use 'url' as the name for this parameter, it's actually the path - it does not include the scheme/hostname/query, and has been URL-decoded)
	if (path[0] != '/' || strstr(path, "/..") ||
	    snprintf(fs_path, sizeof(fs_path), "%s%s", ROOT, path) >= sizeof(fs_path)) {
		return send_file_not_found_response(connection);
	}
	
	fprintf(stderr, "opening %s\n", fs_path);
	do { fd = open(fs_path, O_RDONLY); } while (fd < 0 && errno == EINTR);
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
	// TODO: set content-type
	// TODO: set transfer-encoding
	// TODO: add caching headers
	response = MHD_create_response_from_fd_at_offset(st.st_size, fd, 0); // fd will be closed by MHD when the response is destroyed
	if (!response) { // presumably out of memory
		fprintf(stderr, "Couldn't create response from file %s! (out of memory?)\n", fs_path);
		close(fd);
	}
	ret = MHD_queue_response(connection, MHD_HTTP_OK, response); // does nothing and returns our desired MHD_NO if response is NULL
	MHD_destroy_response(response); // does nothing if response is NULL
	return ret;
}

int handle_post_data(
	void *post_data, enum MHD_ValueKind kind, const char *key, const char *filename,
	const char *content_type, const char *transfer_encoding,
	const char *data, uint64_t off, size_t size) {

	struct Upload* upload = (struct Upload*) post_data;
	
	if (strcmp(key, "uploaded_file") == 0) {
		fprintf(stderr, "uploading into %s: %s, %s, %s, %s (%llu, %ld)\n", upload->tempfile_fs_path, key, filename, content_type, transfer_encoding, off, size);
		while (size > 0) {
			ssize_t written = write(upload->tempfile_fd, data, size);
			if (written < 0 && errno == EINTR) continue;
			if (written < 0) {
				fprintf(stderr, "Couldn't write to %s tempfile: %s (%d)\n", upload->tempfile_fs_path, strerror(errno), errno);
				return MHD_NO;
			}
			size -= (size_t)written;
			data += written;
		}
	}
	
	return MHD_YES;
}

void free_upload(struct Upload* upload) {
	fprintf(stderr, "freeing upload object\n");
	int ret;
	
	if (upload->pp) MHD_destroy_post_processor(upload->pp); // returns MHD_NO if the processor wasn't finished, but it's freed the memory anyway
	
	if (upload->tempfile_fd >= 0) {
		do { ret = close(upload->tempfile_fd); } while (ret < 0 && ret == EINTR);
		if (ret < 0) { // should never happen
			fprintf(stderr, "Failed to close upload tempfile!: %s (%d)\n", strerror(errno), errno);
		}
		
		do { ret = unlink(upload->tempfile_fs_path); } while (ret < 0 && ret == EINTR);
		if (ret < 0) { // should never happen
			fprintf(stderr, "Failed to unlink upload tempfile %s!: %s (%d)\n", upload->tempfile_fs_path, strerror(errno), errno);
		}
	}

	free(upload);
}

struct Upload* create_upload(struct MHD_Connection* connection) {
	fprintf(stderr, "creating upload object\n");
	struct Upload* upload = malloc(sizeof(struct Upload));
	if (!upload) {
		fprintf(stderr, "Couldn't allocate an Upload record! (out of memory?)\n");
		return NULL;
	}
	upload->tempfile_fs_path[0] = 0;
	upload->tempfile_fd = -1;
	upload->pp = NULL;
	
	upload->pp = MHD_create_post_processor(connection, POST_BUFFER_SIZE, &handle_post_data, upload);
	if (!upload->pp) { // presumably out of memory
		fprintf(stderr, "Couldn't create a post processor! (out of memory?)\n");
		free_upload(upload);
		return NULL;
	}
	
	snprintf(upload->tempfile_fs_path, sizeof(upload->tempfile_fs_path), "%s/upload.XXXXXXXX", ROOT);
	do { upload->tempfile_fd = mkstemp(upload->tempfile_fs_path); } while (upload->tempfile_fd == -1 && errno == EINTR);
	if (upload->tempfile_fd < 0) {
		fprintf(stderr, "Couldn't create a %s tempfile: %s (%d)\n", upload->tempfile_fs_path, strerror(errno), errno);
		free_upload(upload);
		return NULL;
	}
	
	return upload;
}

int process_upload_data(struct Upload* upload, const char *upload_data, size_t *upload_data_size) {
	if (MHD_post_process(upload->pp, upload_data, *upload_data_size) != MHD_YES) return MHD_NO;
	*upload_data_size = 0;
	return MHD_YES;
}

void complete_upload(struct Upload* upload) {
	// TODO: complete hashing the file
	// TODO: link into hashed name
}

int handle_post_request(
	void *_daemon_data, struct MHD_Connection* connection,
    const char *path,
    const char *upload_data, size_t *upload_data_size,
	void **request_data) {
	
	fprintf(stderr, "handle_post_request with %ld bytes, request_data set %d, upload_data set %d\n", *upload_data_size, (*request_data ? 1 : 0), (upload_data ? 1 : 0));

	if (!*request_data) { // new connection
		*request_data = create_upload(connection);
		return *request_data ? MHD_YES : MHD_NO;
	}
	
	if (*upload_data_size > 0) { // TODO: is is true that upload_data_size is always set?
	 	return process_upload_data((struct Upload*) *request_data, upload_data, upload_data_size);
	} else {
		fprintf(stderr, "completing upload\n");
		complete_upload((struct Upload*) *request_data);
		return send_redirect(connection, MHD_HTTP_CREATED, "/success");
	}
}
		
int handle_request(
	void* _daemon_data, struct MHD_Connection* connection,
    const char* path, const char* method, const char* version,
    const char* upload_data, size_t* upload_data_size,
	void** request_data) {
	
	if (strcmp(method, "GET") == 0) {
		return handle_get_request(_daemon_data, connection, path, request_data);
		
	} else if (strcmp(method, "POST") == 0) {
		return handle_post_request(request_data, connection, path, upload_data, upload_data_size, request_data);
		
	} else {
		return MHD_NO;
	}
}

int handle_request_completed(
	void *_daemon_data,
	struct MHD_Connection *connection,
	void **request_data,
	enum MHD_RequestTerminationCode toe) {
	
	if (*request_data) {
		free_upload((struct Upload*) *request_data);
		*request_data = NULL;
	}
	
	return MHD_YES;
}

int main(int argc, const char* argv[]) {
	struct MHD_Daemon* daemon;
	
	daemon = MHD_start_daemon(
		MHD_USE_THREAD_PER_CONNECTION | EXTRA_DAEMON_FLAGS,
		HTTP_PORT,
		NULL, NULL, // no connection address check
		&handle_request, NULL, // no extra argument to handle_request
		MHD_OPTION_NOTIFY_COMPLETED, &handle_request_completed, NULL, // no extra argument to handle_request
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
