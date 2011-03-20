/**
 * This file is a part of verm
 * Copyright (c) Will Bryant, Sekuda Limited 2011
 */

#define HTTP_PORT 1138
#define HTTP_TIMEOUT 60
#define POST_BUFFER_SIZE 65536
#define MAX_DIRECTORY_LENGTH 256
#define MAX_PATH_LENGTH 512 // checked; enough for /data root directory:200/client-requested directory:256/hashed filename:44.extension:8
#define DIRECTORY_PERMISSION 0777
#define DEBUG

#define ROOT "/var/lib/verm"
#define DEFAULT_DIRECTORY "/default" // rather than letting people upload directly into the root directory, which in practice is a PITA to administer

#define HTTP_404_PAGE "<!DOCTYPE html><html><head><title>Verm - File not found</title></head><body>File not found</body></html>"
#define UPLOAD_PAGE "<!DOCTYPE html><html><head><title>Verm - Upload</title></head><body>" \
                    "<form method='post' enctype='multipart/form-data'>" \
                    "<input type='hidden' name='redirect' value='1'/>" /* redirect instead of returning 201 (as APIs want) */ \
                    "<input type='file' name='uploaded_file'/>" \
                    "<input type='submit' value='Upload'/>" \
                    "</form>" \
                    "</body></html>\n"
#define CREATED_PAGE "Resource created\n"
#define REDIRECT_PAGE "You are being redirected\n"

#include "platform.h"
#include "microhttpd.h"
#include <openssl/sha.h>
#include "mime_types.h"

#ifdef DEBUG
	#define EXTRA_DAEMON_FLAGS MHD_USE_DEBUG
#else
	#define EXTRA_DAEMON_FLAGS 0
#endif

struct Upload {
	char directory[MAX_DIRECTORY_LENGTH];
	char tempfile_fs_path[MAX_PATH_LENGTH];
	int tempfile_fd;
	size_t size;
	struct MHD_PostProcessor* pp;
	SHA256_CTX hasher;
	const char* extension;
	char final_fs_path[MAX_PATH_LENGTH];
	int redirect_afterwards;
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

int send_not_modified_response(struct MHD_Connection* connection, const char* etag) {
	struct MHD_Response* response;
	int ret;
	
	response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
	ret = MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, etag) &&
	      MHD_queue_response(connection, MHD_HTTP_NOT_MODIFIED, response); // cleanly returns MHD_NO if response was NULL for any reason
	MHD_destroy_response(response); // does nothing if response was NULL for any reason
	return ret;
}

int send_redirect(struct MHD_Connection* connection, unsigned int status_code, char* location, char* page) {
	struct MHD_Response* response;
	int ret;
	
	response = MHD_create_response_from_buffer(strlen(page), page, MHD_RESPMEM_PERSISTENT);
	ret = MHD_add_response_header(response, "Location", location) &&
	      MHD_queue_response(connection, status_code, response); // cleanly returns MHD_NO if response was NULL for any reason
	MHD_destroy_response(response); // does nothing if response was NULL for any reason
	return ret;
}

int add_content_length(struct MHD_Response* response, size_t content_length) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%lu", content_length);
	return MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_LENGTH, buf);
}

int add_last_modified(struct MHD_Response* response, time_t last_modified) {
	char buf[64];
	struct tm t;
	gmtime_r(&last_modified, &t);
	strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &t);
	return MHD_add_response_header(response, MHD_HTTP_HEADER_LAST_MODIFIED, buf);
}

int add_content_type(struct MHD_Response* response, const char* filename) {
	const char* found;
	
	// ignore the directory path part
	found = strrchr(filename, '/');
	if (found) filename = found + 1;

	// find the extension separator
	found = strchr(filename, '.');
	
	// and if an extension is indeed present, lookup and add the mime type for that extension (if any)
	if (found) {
		found = mime_type_for_extension(found + 1);
		if (found) return MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, found);
	}

	// no extension or no corresponding mime type definition, don't add a content-type header but carry on
	return MHD_YES;
}

int handle_get_or_head_request(
	void* _daemon_data, struct MHD_Connection* connection,
    const char* path, void** _request_data, int send_data) {

	int fd;
	struct stat st;
	struct MHD_Response* response;
	int ret;
	char fs_path[MAX_PATH_LENGTH];
	const char* request_value;

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
	
	if (st.st_mode & S_IFDIR) {
		close(fd);
		return send_static_page_response(connection, MHD_HTTP_OK, UPLOAD_PAGE);
	}
	
	if ((request_value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_NONE_MATCH)) &&
	    strcmp(request_value, path + 1) == 0) {
		fprintf(stderr, "%s not modified\n", path);
		return send_not_modified_response(connection, path + 1); // to match the ETag we issue below
	}
	
	// FUTURE: support range requests
	// TODO: set transfer-encoding
	if (send_data) {
		// ie. a GET request
		response = MHD_create_response_from_fd_at_offset(st.st_size, fd, 0); // fd will be closed by MHD when the response is destroyed
	} else {
		// ie. a HEAD request
		response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		if (response) close(fd);
	}
	
	if (!response) { // presumably out of memory
		fprintf(stderr, "Couldn't create response from file %s! (out of memory?)\n", fs_path);
		close(fd);
	}
	ret = add_content_length(response, st.st_size) &&
	      add_last_modified(response, st.st_mtime) &&
	      add_content_type(response, path) &&
	      MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, path + 1) && // since the path includes the hash, it's a perfect ETag
	      MHD_add_response_header(response, MHD_HTTP_HEADER_EXPIRES, "Tue, 19 Jan 2038 00:00:00"), // essentially never expires
	      MHD_queue_response(connection, MHD_HTTP_OK, response); // does nothing and returns our desired MHD_NO if response is NULL
	MHD_destroy_response(response); // does nothing if response is NULL
	return ret;
}

int boolean(const char *data, size_t size) {
	return (strncmp("0", data, size) != 0 &&
			strncasecmp("f", data, size) != 0 &&
			strncasecmp("false", data, size) != 0);
}

int handle_post_data(
	void *post_data, enum MHD_ValueKind kind, const char *key, const char *filename,
	const char *content_type, const char *transfer_encoding,
	const char *data, uint64_t offset, size_t size) {

	struct Upload* upload = (struct Upload*) post_data;
	
	if (strcmp(key, "uploaded_file") == 0) {
		fprintf(stderr, "uploading into %s: %s, %s, %s, %s (%llu, %ld)\n", upload->tempfile_fs_path, key, filename, content_type, transfer_encoding, offset, size);
		SHA256_Update(&upload->hasher, (unsigned char*)data, size);
		upload->size += size;
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
		
		if (offset == 0 && content_type) {
			fprintf(stderr, "Looking up extension for %s\n", content_type);
			upload->extension = extension_for_mime_type(content_type);
		}
	
	} else if (strcmp(key, "redirect") == 0) {
		if (offset == 0) {
			upload->redirect_afterwards = boolean(data, size);
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

struct Upload* create_upload(struct MHD_Connection *connection, const char *path) {
	char* s;
	
	if (path[0] != '/' || strstr(path, "/..") || strlen(path) >= MAX_DIRECTORY_LENGTH) {
		fprintf(stderr, "Refusing post to a suspicious path: '%s'\n", path);
		return NULL;
	}
	
	fprintf(stderr, "creating upload object\n");
	struct Upload* upload = malloc(sizeof(struct Upload));
	if (!upload) {
		fprintf(stderr, "Couldn't allocate an Upload record! (out of memory?)\n");
		return NULL;
	}
	upload->tempfile_fs_path[0] = 0;
	upload->tempfile_fd = -1;
	upload->size = 0;
	upload->pp = NULL;
	upload->extension = NULL;
	upload->final_fs_path[0] = 0;
	upload->redirect_afterwards = 0;
	
	strncpy(upload->directory, path, sizeof(upload->directory)); // length was checked above, but easier to audit if we never call strcpy!
	while (s = strstr(upload->directory, "//")) memmove(s, s + 1, strlen(s)); // replace a//b with a/b

	if (upload->directory[1]) {
		// path given
		s = upload->directory + strlen(upload->directory) - 1;
		if (s != upload->directory && *s == '/') *s = 0; // normalise /foo/ to /foo, but don't touch /foo or /
	} else {
		// path not given; use the default
		strncpy(upload->directory, DEFAULT_DIRECTORY, sizeof(upload->directory));
	}
	
	SHA256_Init(&upload->hasher);
	
	upload->pp = MHD_create_post_processor(connection, POST_BUFFER_SIZE, &handle_post_data, upload);
	if (!upload->pp) { // presumably out of memory
		fprintf(stderr, "Couldn't create a post processor! (out of memory?)\n");
		free_upload(upload);
		return NULL;
	}
	
	snprintf(upload->tempfile_fs_path, sizeof(upload->tempfile_fs_path), "%s%s/upload.XXXXXXXX", ROOT, upload->directory);
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

int same_file_contents(int fd1, int fd2, size_t size) {
	char buf1[16384], buf2[16384];
	int ret1, ret2;

	off_t offset = 0;
	while (offset < size) {
		do { ret1 = pread(fd1, buf1, sizeof(buf1), offset); } while (ret1 == -1 && errno == EINTR);
		do { ret2 = pread(fd2, buf2, sizeof(buf2), offset); } while (ret2 == -1 && errno == EINTR);
		if (ret1 != ret2 || memcmp(buf1, buf2, ret1) != 0) return 0;
		offset += ret1;
	}

	return 1;
}

int link_file(struct Upload* upload, char* encoded) {
	int ret;
	int attempt = 1;
	struct stat st;
	char final_directory[MAX_PATH_LENGTH];
	int created_directory = 0;
	
	// we put each file in a subdirectory off the main root, whose name is the first two characters of the hash.
	// we don't repeat those characters in the filename.
	ret = snprintf(final_directory, sizeof(final_directory), "%s%s/%.2s", ROOT, upload->directory, encoded);
	if (ret >= sizeof(final_directory)) { // shouldn't be possible unless misconfigured
		fprintf(stderr, "Couldn't generate directory for %s under %s%s within limits\n", encoded, ROOT, upload->directory);
		return -1;
	}
	encoded += 2;
	
	if (upload->extension) {
		ret = snprintf(upload->final_fs_path, sizeof(upload->final_fs_path), "%s/%s.%s", final_directory, encoded, upload->extension);
	} else {
		ret = snprintf(upload->final_fs_path, sizeof(upload->final_fs_path), "%s/%s",    final_directory, encoded);
	}

	while (1) {
		if (ret >= sizeof(upload->final_fs_path)) { // shouldn't possible unless misconfigured
			fprintf(stderr, "Couldn't generate filename for %s under %s within limits\n", upload->tempfile_fs_path, ROOT);
			return -1;
		}
		
		fprintf(stderr, "trying to link as %s\n", upload->final_fs_path);
		do { ret = link(upload->tempfile_fs_path, upload->final_fs_path); } while (ret < 0 && errno == EINTR);
		if (ret == 0) break; // successfully linked
		
		if (errno != EEXIST) {
			if (errno == ENOENT && !created_directory) {
				do { ret = mkdir(final_directory, DIRECTORY_PERMISSION); } while (ret < 0 && errno == EINTR);
				if (ret != 0 && errno != EEXIST) {
					fprintf(stderr, "Couldn't create %s: %s (%d)\n", final_directory, strerror(errno), errno);
					return -1;
				}
				created_directory = 1;
				continue;
			}
			
			fprintf(stderr, "Couldn't link %s to %s: %s (%d)\n", upload->final_fs_path, upload->tempfile_fs_path, strerror(errno), errno);
			return -1;
		}
		
		// so the file already exists; is it exactly the same file?
		if (stat(upload->final_fs_path, &st) < 0) {
			fprintf(stderr, "Couldn't stat pre-existing file %s: %s (%d)\n", upload->final_fs_path, strerror(errno), errno);
			return -1;
		}
				
		if (st.st_size == upload->size) {
			int fd2, same;
			do { fd2 = open(upload->final_fs_path, O_RDONLY); } while (fd2 == -1 && errno == EINTR);
			same = same_file_contents(upload->tempfile_fd, fd2, upload->size);
			do { ret = close(fd2); } while (ret == -1 && errno == EINTR);
			
			if (same) break; // same file size and contents
		}
		
		// no, different file; loop around and try again, this time with an attempt number appended to the end
		if (upload->extension) {
			ret = snprintf(upload->final_fs_path, sizeof(upload->final_fs_path), "%s/%s_%d.%s", final_directory, encoded, ++attempt, upload->extension);
		} else {
			ret = snprintf(upload->final_fs_path, sizeof(upload->final_fs_path), "%s/%s_%d",    final_directory, encoded, ++attempt);
		}
	}
	
	return 0;
}

int complete_upload(struct Upload* upload) {
	static const char encode_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

	unsigned char md[SHA256_DIGEST_LENGTH];
	unsigned char* src = md;
	unsigned char* end = md + SHA256_DIGEST_LENGTH;

	char encoded[45]; // for 32 input bytes, we need 45 output bytes (ceil(32/3.0)*4 rounded up, plus a null terminator byte)
	char* dest = encoded;
	
	SHA256_Final(md, &upload->hasher);

	while (src < end) {
		unsigned char s0 = *src++;
		unsigned char s1 = (src == end) ? 0 : *src++;
		unsigned char s2 = (src == end) ? 0 : *src++;
		*dest++ = encode_chars[(s0 & 0xfc) >> 2];
		*dest++ = encode_chars[((s0 & 0x03) << 4) + ((s1 & 0xf0) >> 4)];
		*dest++ = encode_chars[((s1 & 0x0f) << 2) + ((s2 & 0xc0) >> 6)];
		*dest++ = encode_chars[s2 & 0x3f];
	}
	*dest = 0;

	fprintf(stderr, "hashed, encoded filename is %s\n", encoded);
	return link_file(upload, encoded);
}

int handle_post_request(
	void *_daemon_data, struct MHD_Connection* connection,
    const char *path,
    const char *upload_data, size_t *upload_data_size,
	void **request_data) {
	
	fprintf(stderr, "handle_post_request to %s with %ld bytes, request_data set %d, upload_data set %d\n", path, *upload_data_size, (*request_data ? 1 : 0), (upload_data ? 1 : 0));

	if (!*request_data) { // new connection
		*request_data = create_upload(connection, path);
		return *request_data ? MHD_YES : MHD_NO;
	}
	
	struct Upload* upload = (struct Upload*) *request_data;
	if (*upload_data_size > 0) { // TODO: is is true that upload_data_size is always set?
	 	return process_upload_data(upload, upload_data, upload_data_size);
	} else {
		fprintf(stderr, "completing upload\n");
		if (complete_upload(upload) < 0) {
			fprintf(stderr, "completing failed\n");
			return MHD_NO;
		} else {
			char* final_relative_path = upload->final_fs_path + strlen(ROOT);
			if (upload->redirect_afterwards) {
				fprintf(stderr, "redirecting to %s\n", final_relative_path);
				return send_redirect(connection, MHD_HTTP_SEE_OTHER, final_relative_path, REDIRECT_PAGE);
			} else {
				fprintf(stderr, "created %s\n", final_relative_path);
				return send_redirect(connection, MHD_HTTP_CREATED, final_relative_path, CREATED_PAGE);
			}
		}
	}
}
		
int handle_request(
	void* _daemon_data, struct MHD_Connection* connection,
    const char* path, const char* method, const char* version,
    const char* upload_data, size_t* upload_data_size,
	void** request_data) {
	
	if (strcmp(method, "GET") == 0) {
		return handle_get_or_head_request(_daemon_data, connection, path, request_data, 1);
		
	} else if (strcmp(method, "HEAD") == 0) {
		return handle_get_or_head_request(_daemon_data, connection, path, request_data, 0);
		
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
	
	load_mime_types();
	#ifdef DEBUG
	dump_mime_types();
	#endif
	
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
