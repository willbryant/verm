#include "settings.h"

#define HTTP_TIMEOUT 60
#define POST_BUFFER_SIZE 65536
#define DIRECTORY_PERMISSION 0777
#undef  DUMP_MIME_TYPES

#define DEFAULT_ROOT "/var/lib/verm"
#define DIRECTORY_IF_NOT_GIVEN_BY_CLIENT "/default" // rather than letting people upload directly into the root directory, which in practice is a PITA to administer.  no command-line option for this because it should be provided by the client, so letting admins change it implies mis-use by the client which would be a problem down the track.

#define UPLOADED_FILE_FIELD_NAME "uploaded_file"
#define STATISTICS_PATH "/_statistics"

#include "platform.h"
#include "microhttpd.h"
#include <openssl/sha.h>
#include "responses.h"
#include "response_logging.h"
#include "decompression.h"
#include "str.h"
#include "mime_types.h"

#ifdef DEBUG
	#define EXTRA_DAEMON_FLAGS MHD_USE_DEBUG
#else
	#define EXTRA_DAEMON_FLAGS 0
#endif

#define ERR_PUT_TO_WRONG_PATH -2

struct Server {
	int quiet;
	char* root_data_directory;
};

struct Upload {
	char directory[MAX_DIRECTORY_LENGTH];
	char tempfile_fs_path[MAX_PATH_LENGTH];
	int tempfile_fd;
	size_t size;
	struct MHD_PostProcessor* pp;
	SHA256_CTX hasher;
	void* decompressor;
	const char* extension;
	const char* encoding;
	const char* encoding_suffix;
	char location[MAX_PATH_LENGTH];
	int redirect_afterwards;
	int new_file_stored;
};

const char* dummy_to_indicate_second_call = "not-null";
const char* dummy_to_indicate_statistics_request = STATISTICS_PATH;

int handle_get_or_head_request(
	struct Server* server, struct MHD_Connection* connection,
    const char* path, void** request_data, int send_data) {

	int fd;
	struct stat st;
	off_t file_size;
	struct MHD_Response* response;
	int ret;
	char fs_path[MAX_PATH_LENGTH];
	const char* request_value;
	int requested_uncompressed = 0;

	// later versions of libmicrohttpd misbehave if we queue a response immediately on receiving the request;
	// we are supposed to wait until the second call
	if (NULL == *request_data) {
		*request_data = (void*) dummy_to_indicate_second_call;
		return MHD_YES;
	}

	if (strcmp(path, "/") == 0) {
		return send_upload_page_response(connection);

	} else if (strcmp(path, STATISTICS_PATH) == 0) {
		*request_data = (void*) dummy_to_indicate_statistics_request;
		return handle_statistics_request(connection);
	}
	
	// check and expand the path (although the MHD docs use 'url' as the name for this parameter, it's actually the path - it does not include the scheme/hostname/query, and has been URL-decoded)
	if (path[0] != '/' || strstr(path, "/..") ||
	    snprintf(fs_path, sizeof(fs_path), "%s%s", server->root_data_directory, path) >= sizeof(fs_path)) {
		return send_file_not_found_response(connection);
	}
	
	DEBUG_PRINT("trying to open %s\n", fs_path);
	do { fd = open(fs_path, O_RDONLY); } while (fd < 0 && errno == EINTR);
	if (fd < 0) {
		switch (errno) {
			case ENOENT:
			case EACCES:
				if (strendswith(fs_path, ".gz") || // if the client asked for a .gz, don't try .gz.gz
					snprintf(fs_path, sizeof(fs_path), "%s%s.gz", server->root_data_directory, path) >= sizeof(fs_path)) {
					return send_file_not_found_response(connection);
				} else {
					DEBUG_PRINT("trying to open %s\n", fs_path);
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
					DEBUG_PRINT("opened %s\n", fs_path);
					
					requested_uncompressed = 1;
				}
				break;
			
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
		return send_upload_page_response(connection);
	}

	file_size = st.st_size;
	
	if ((request_value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_NONE_MATCH)) &&
	    strcmp(request_value, path + 1) == 0) {
		DEBUG_PRINT("%s not modified\n", path);
		return send_not_modified_response(connection, path + 1); // to match the ETag we issue below
	}
	
	// FUTURE: support range requests
	if (send_data) {
		// ie. a GET request
		if (requested_uncompressed && !accept_gzip_encoding(connection)) {
			// the file is compressed, but the client explicitly told us they don't support that, so decompress the file
			file_size = get_decompressed_file_size(fd, st.st_size);
			void* decompression = create_file_decompressor(fd);
			if (!decompression) return MHD_NO; // out of memory
			response = MHD_create_response_from_callback(file_size, DECOMPRESSION_CHUNK, &decompress_file_chunk, decompression, &destroy_file_decompressor);
		} else {
			// the file is either not compressed, or is compressed and the client supports that, so we can serve the file as-is
			response = MHD_create_response_from_fd_at_offset(st.st_size, fd, 0); // fd will be closed by MHD when the response is destroyed
			
			// if the file is compressed, then we need to add a header saying so
			if (requested_uncompressed && !add_gzip_content_encoding(response)) return MHD_NO; // out of memory
		}
	} else {
		// ie. a HEAD request
		response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		if (response) close(fd);
	}
	
	if (!response) { // presumably out of memory
		fprintf(stderr, "Couldn't create response from file %s! (out of memory?)\n", fs_path);
		close(fd);
	}
	ret = add_content_length(response, file_size) &&
	      add_last_modified(response, st.st_mtime) &&
	      add_content_type(response, path) &&
	      MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, path + 1) && // since the path includes the hash, it's a perfect ETag
	      MHD_add_response_header(response, MHD_HTTP_HEADER_EXPIRES, "Tue, 19 Jan 2038 00:00:00"), // essentially never expires
	      MHD_queue_response(connection, MHD_HTTP_OK, response); // does nothing and returns our desired MHD_NO if response is NULL
	MHD_destroy_response(response); // does nothing if response is NULL
	return ret;
}

int handle_statistics_request(struct MHD_Connection* connection) {
	struct MHD_Response* response;
	int ret;
	char *buffer;
	
	buffer = create_log_statistics_string(connection);
	if (buffer == NULL) return MHD_NO;
	
	response = MHD_create_response_from_buffer(strlen(buffer), buffer, MHD_RESPMEM_MUST_FREE);
	ret = MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain") &&
		  MHD_queue_response(connection, MHD_HTTP_OK, response); // cleanly returns MHD_NO if response was NULL for any reason
	MHD_destroy_response(response); // does nothing if response was NULL for any reason
	return ret;
}

void update_upload_hash(struct Upload* upload, const char *data, size_t size) {
	SHA256_Update(&upload->hasher, (unsigned char*)data, size);
}

int decompress_and_update_upload_hash(struct Upload* upload, const char *data, size_t size) {
	char decompressed[16384];
	ssize_t decompressed_size;

	while (1) {
		decompressed_size = decompress_memory_chunk(upload->decompressor, &data, &size, decompressed, sizeof(decompressed));
		if (decompressed_size == -1) return 1;
		if (decompressed_size == 0) break;
		update_upload_hash(upload, decompressed, decompressed_size);
	}

	return 0;
}

int handle_post_data(
	void *post_data, enum MHD_ValueKind kind, const char *key, const char *filename,
	const char *content_type, const char *content_encoding,
	const char *data, uint64_t offset, size_t size) {

	struct Upload* upload = (struct Upload*) post_data;
	
	if (strcmp(key, UPLOADED_FILE_FIELD_NAME) == 0) {
		if (offset == 0) {
			if (content_type) {
				upload->extension = extension_for_mime_type(content_type);
				if (!upload->extension) upload->extension = "";
				DEBUG_PRINT("Extension for content-type %s is %s\n", content_type, upload->extension);
			}
			if (content_encoding) {
				if (strcmp(content_encoding, "gzip") == 0) {
					upload->encoding = "gzip"; // static string so doesn't need to be copied
					upload->encoding_suffix = ".gz"; // static string so doesn't need to be copied
					upload->decompressor = create_memory_decompressor(); // we have to decompress the file to hash it and determine the filename (but we save the wire content as it is, so the files should be binary-identical even if zlib is set up differently)
					if (!upload->decompressor) return MHD_NO; // presumably out of memory; create_memory_decompressor() has already printed an error
				}
				DEBUG_PRINT("Extension suffix for encoding %s is %s\n", content_encoding, upload->encoding_suffix);
			}
		}
	
		// write to the tempfile
		DEBUG_PRINT("uploading into %s: %s, %s, %s, %s (%llu, %ld)\n", upload->tempfile_fs_path, key, filename, content_type, content_encoding, offset, size);
		if (upload->decompressor) {
			if (decompress_and_update_upload_hash(upload, data, size)) return MHD_NO;
		} else {
			update_upload_hash(upload, data, size);
		}
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
		
	} else if (strcmp(key, "redirect") == 0) {
		if (offset == 0) {
			upload->redirect_afterwards = boolean(data, size);
		}
	}
	
	return MHD_YES;
}

void free_upload(struct Upload* upload) {
	DEBUG_PRINT("freeing upload object\n", NULL);
	int ret;

	if (upload->decompressor) destroy_memory_decompressor(upload->decompressor);
	
	if (upload->pp) MHD_destroy_post_processor(upload->pp); // returns MHD_NO if the processor wasn't finished, but it's freed the memory anyway
	
	if (upload->tempfile_fd >= 0) {
		do { ret = close(upload->tempfile_fd); } while (ret < 0 && errno == EINTR);
		if (ret < 0) { // should never happen
			fprintf(stderr, "Failed to close upload tempfile!: %s (%d)\n", strerror(errno), errno);
		}
		
		do { ret = unlink(upload->tempfile_fs_path); } while (ret < 0 && errno == EINTR);
		if (ret < 0) { // should never happen
			fprintf(stderr, "Failed to unlink upload tempfile %s!: %s (%d)\n", upload->tempfile_fs_path, strerror(errno), errno);
		}
	}

	free(upload);
}

void _try_make_tempfile(struct Upload* upload, const char* root_data_directory) {
	/* annoyingly, glibc's implementation of mkstemp overwrites the XXXXXXXX part of the template before attempting to
	   open the directory.  consequently, subsequent calls to mkstemp would fail on Linux if we did not re-sprintf the
	   template string, so we have extracted these two lines out to this method to avoid needing to duplicate the
	   sprintf in the two places it's used below. */
	snprintf(upload->tempfile_fs_path, sizeof(upload->tempfile_fs_path), "%s%s/upload.XXXXXX", root_data_directory, upload->directory);
	do { upload->tempfile_fd = mkstemp(upload->tempfile_fs_path); } while (upload->tempfile_fd == -1 && errno == EINTR);
}

int create_parent_directories_in_path(char* path, int root_directory_length) {
	char *sep;
	int ret;
	
	for (sep = path + root_directory_length; sep; sep = strchr(sep + 1, '/')) {
		*sep = 0;
		do { ret = mkdir(path, DIRECTORY_PERMISSION); } while (ret < 0 && errno == EINTR);
		if (ret != 0 && errno != EEXIST) { // EEXIST would just mean another process beat us to it
			fprintf(stderr, "Couldn't create %s: %s (%d)\n", path, strerror(errno), errno);
			*sep = '/';
			return -1;
		}
		*sep = '/';
	}
	
	return 0;
}

struct Upload* create_upload(struct MHD_Connection *connection, const char* root_data_directory, const char *path, int posting) {
	const char *request_value, *separator;
	char *s;
	
	if (path[0] != '/' || strstr(path, "/..") || strlen(path) >= (posting ? MAX_DIRECTORY_LENGTH : MAX_PATH_LENGTH)) {
		fprintf(stderr, "Refusing %s to a suspicious path: '%s'\n", posting ? "post" : "put", path);
		send_forbidden_wrong_path_response(connection);
		return NULL;
	}
	
	DEBUG_PRINT("creating upload object\n", NULL);
	struct Upload* upload = malloc(sizeof(struct Upload));
	if (!upload) {
		fprintf(stderr, "Couldn't allocate an Upload record! (out of memory?)\n");
		return NULL;
	}
	upload->tempfile_fs_path[0] = 0;
	upload->tempfile_fd = -1;
	upload->size = 0;
	upload->pp = NULL;
	upload->decompressor = NULL;
	upload->extension = "";
	upload->encoding = "";
	upload->encoding_suffix = "";
	upload->location[0] = 0;
	upload->redirect_afterwards = 0;
	upload->new_file_stored = 0;
	
	if (posting) {
		strncpy(upload->directory, path, sizeof(upload->directory)); // length was checked above, but easier to audit if we never call strcpy!
		while (s = strstr(upload->directory, "//")) memmove(s, s + 1, strlen(s)); // replace a//b with a/b
	} else {
		separator = strr2ndchr(path + 1, '/');
		if (separator == NULL || separator == path + 1 || separator - path >= MAX_DIRECTORY_LENGTH || !*(separator + 1) || strstr(path, "//")) {
			send_forbidden_wrong_path_response(connection);
			return NULL;
		}
		snprintf(upload->directory, separator - path + 1, "%s", path);
		strncpy(upload->location, path, sizeof(upload->location)); // length was checked above, but easier to audit if we never call strcpy!
	}

	if (upload->directory[1]) {
		// path given
		s = upload->directory + strlen(upload->directory) - 1;
		if (s != upload->directory && *s == '/') *s = 0; // normalise /foo/ to /foo, but don't touch /foo or /
	} else {
		// path not given; use the default
		strncpy(upload->directory, DIRECTORY_IF_NOT_GIVEN_BY_CLIENT, sizeof(upload->directory));
	}
	
	SHA256_Init(&upload->hasher);
	
	if ((request_value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_TYPE)) &&
	    (strncasecmp(request_value, MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA, strlen(MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA)) == 0 ||
	     strncasecmp(request_value, MHD_HTTP_POST_ENCODING_FORM_URLENCODED,    strlen(MHD_HTTP_POST_ENCODING_FORM_URLENCODED))    == 0)) {
	    // an encoded form
		upload->pp = MHD_create_post_processor(connection, POST_BUFFER_SIZE, &handle_post_data, upload);
		if (!upload->pp) { // presumably out of memory
			fprintf(stderr, "Couldn't create a post processor! (out of memory?)\n");
			free_upload(upload);
			return NULL;
		}
	}
	
	_try_make_tempfile(upload, root_data_directory);
	
	if (upload->tempfile_fd < 0 && errno == ENOENT) {
		// create the directory (or directories, if nested)
		if (create_parent_directories_in_path(upload->tempfile_fs_path, strlen(root_data_directory)) < 0) {
			free_upload(upload);
			return NULL;
		}

		// try again
		_try_make_tempfile(upload, root_data_directory);
	}
	
	if (upload->tempfile_fd < 0) {
		fprintf(stderr, "Couldn't create a %s tempfile: %s (%d)\n", upload->tempfile_fs_path, strerror(errno), errno);
		free_upload(upload);
		return NULL;
	}
	
	return upload;
}

int process_upload_data(struct MHD_Connection* connection, struct Upload* upload, const char *upload_data, size_t *upload_data_size) {
	const char *content_type = NULL;
	const char *content_encoding = NULL;

	if (upload->pp) {
		// encoded form
		if (MHD_post_process(upload->pp, upload_data, *upload_data_size) != MHD_YES) return MHD_NO;
	} else {
		// raw POST
		if (upload->size == 0) {
			// first call, need to pass in the content headers
			content_type     = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_TYPE);
			content_encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_ENCODING);
		}
		if (handle_post_data(upload, MHD_POSTDATA_KIND, UPLOADED_FILE_FIELD_NAME, NULL, content_type, content_encoding,
		                     upload_data, upload->size, *upload_data_size) != MHD_YES) return MHD_NO;
	}
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

int link_file(struct Upload* upload, const char* root_data_directory, char* encoded) {
	int ret;
	int attempt = 1;
	struct stat st;
	char final_fs_path[MAX_PATH_LENGTH];
	int dl, sl;
	
	if (!upload->location[0]) {
		// we put each file in a subdirectory off the main root, whose name is the first two characters of the hash.
		// we don't repeat those characters in the filename.
		ret = snprintf(upload->location, sizeof(upload->location), "%s/%.2s/%s%s", upload->directory, encoded, encoded + 2, upload->extension);
	} else {
		// check that the given location is correct
		dl = strlen(upload->directory);
		sl = strlen(encoded);
		if (strlen(upload->location) < dl + sl + 2 ||
		    strncmp(upload->location, upload->directory, dl) != 0 ||
		    *(upload->location + dl) != '/' ||
		    *(upload->location + dl + 1) != encoded[0] ||
		    *(upload->location + dl + 2) != encoded[1] ||
		    *(upload->location + dl + 3) != '/' ||
		    strncmp(upload->location + dl + 4, encoded + 2, sl - 2) ||
		    (*(upload->location + dl + sl + 2) != '\0' &&
		     (*(upload->location + dl + sl + 2) != '.' || strchr(upload->location + dl + sl + 3, '.')))) {
		    // PUT to an incorrect path; return a 409
	    	return ERR_PUT_TO_WRONG_PATH;
	    }
	    ret = 0;
	}

	while (1) {
		if (ret >= sizeof(upload->location) || // shouldn't possible unless misconfigured
		    snprintf(final_fs_path, sizeof(final_fs_path), "%s%s%s", root_data_directory, upload->location, upload->encoding_suffix) >= sizeof(final_fs_path)) { // same
			fprintf(stderr, "Couldn't generate filename for %s under %s within limits\n", upload->tempfile_fs_path, root_data_directory);
			return -1;
		}
		
		DEBUG_PRINT("trying to link as %s\n", final_fs_path);
		do { ret = link(upload->tempfile_fs_path, final_fs_path); } while (ret < 0 && errno == EINTR);

		if (ret == 0) {
			// successfully linked
			upload->new_file_stored = 1;
			add_replication_file(upload->location, final_fs_path, upload->encoding);
			break;
		
		} else if (errno == EEXIST) {
			// so the file already exists; is it exactly the same file?
			if (stat(final_fs_path, &st) < 0) {
				fprintf(stderr, "Couldn't stat pre-existing file %s: %s (%d)\n", final_fs_path, strerror(errno), errno);
				return -1;
			}
				
			if (st.st_size == upload->size) {
				int fd2, same;
				do { fd2 = open(final_fs_path, O_RDONLY); } while (fd2 == -1 && errno == EINTR);
				same = same_file_contents(upload->tempfile_fd, fd2, upload->size);
				do { ret = close(fd2); } while (ret == -1 && errno == EINTR);
			
				if (same) break; // same file size and contents
				// note that we have not set upload->new_file_stored - as the name implies, that is for counting new files
			}
		
			// no, different file; loop around and try again, this time with an attempt number appended to the end
			ret = snprintf(upload->location, sizeof(upload->location), "%s/%.2s/%s_%d%s", upload->directory, encoded, encoded + 2, ++attempt, upload->extension);
		
		} else if (errno == ENOENT) {
			// need to create the parent directory for the file - perfectly normal, since we make 64*64 subdirectories off the requested data directory, but only make them as required
			if (create_parent_directories_in_path(final_fs_path, strlen(root_data_directory)) < 0) {
				return -1;
			}
			ret = 0;
			
		} else {
			fprintf(stderr, "Couldn't link %s to %s: %s (%d)\n", final_fs_path, upload->tempfile_fs_path, strerror(errno), errno);
			return -1;
		}
	}
	
	return 0;
}

int complete_upload(struct Upload* upload, const char* root_data_directory) {
	static const char encode_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

	unsigned char md[SHA256_DIGEST_LENGTH];
	unsigned char* src = md;
	unsigned char* end = md + SHA256_DIGEST_LENGTH;

	char encoded[45]; // for 32 input bytes, we need 45 output bytes (ceil(32/3.0)*4, plus a null terminator byte)
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

	DEBUG_PRINT("hashed, encoded filename is %s\n", encoded);
	return link_file(upload, root_data_directory, encoded);
}

int handle_post_or_put_request(
	struct Server* server, struct MHD_Connection* connection,
    const char *path,
    const char *upload_data, size_t *upload_data_size,
	void **request_data,
	int posting) {
	
	DEBUG_PRINT("handle_post_request to %s with %ld bytes, request_data set %d, upload_data set %d, %s\n", path, *upload_data_size, (*request_data ? 1 : 0), (upload_data ? 1 : 0), posting ? "posting" : "putting");

	if (!*request_data) { // new request
		*request_data = create_upload(connection, server->root_data_directory, path, posting);
		return (*request_data || responded(connection)) ? MHD_YES : MHD_NO;
	}
	
	struct Upload* upload = (struct Upload*) *request_data;
	if (*upload_data_size > 0) {
	 	return process_upload_data(connection, upload, upload_data, upload_data_size);
	} else {
		DEBUG_PRINT("completing upload\n", NULL);
		switch (complete_upload(upload, server->root_data_directory)) {
			case 0:
				if (upload->redirect_afterwards) {
					DEBUG_PRINT("redirecting to %s\n", upload->location);
					return send_redirected_response(connection, upload->location);
				} else {
					DEBUG_PRINT("created %s\n", upload->location);
					return send_created_response(connection, upload->location);
				}
			
			case ERR_PUT_TO_WRONG_PATH:
				DEBUG_PRINT("put to wrong path\n", NULL);
				return send_forbidden_wrong_path_response(connection);
			
			default:
				DEBUG_PRINT("completing failed\n", NULL);
				return MHD_NO;
		}
	}
}
		
int handle_request(
	void* void_server, struct MHD_Connection* connection,
    const char* path, const char* method, const char* version,
    const char* upload_data, size_t* upload_data_size,
	void** request_data) {
	struct Server* server = (struct Server*) void_server;
	
	if (strcmp(method, "GET") == 0) {
		return handle_get_or_head_request(server, connection, path, request_data, 1);
		
	} else if (strcmp(method, "HEAD") == 0) {
		return handle_get_or_head_request(server, connection, path, request_data, 0);
		
	} else if (strcmp(method, "POST") == 0) {
		return handle_post_or_put_request(server, connection, path, upload_data, upload_data_size, request_data, 1);
		
	} else if (strcmp(method, "PUT") == 0) {
		return handle_post_or_put_request(server, connection, path, upload_data, upload_data_size, request_data, 0);
		
	} else {
		return MHD_NO;
	}
}

int handle_request_completed(
	void* void_server,
	struct MHD_Connection *connection,
	void** request_data,
	enum MHD_RequestTerminationCode toe) {
	struct Server* server = (struct Server*) void_server;
	
	int new_file_stored = 0;
	if (*request_data && *request_data != dummy_to_indicate_second_call && *request_data != dummy_to_indicate_statistics_request) {
		struct Upload *upload = (struct Upload *)*request_data;
		new_file_stored = upload->new_file_stored;
		free_upload(upload);
		*request_data = NULL;
	}

	log_response(connection, server->quiet, *request_data == dummy_to_indicate_statistics_request, new_file_stored);
	
	return MHD_YES;
}

int help() {
	fprintf(stderr, "%s",
		"Usage: verm\n"
		"          - Runs Verm.\n"
		"\n"
		"            Verm requires no privileges except read/write access to the data directory, and should\n"
		"            be run as the user you want to own the files.\n"
		"            It can be run as root, but running daemons as root is generally discouraged.\n"
		"\n"
		"Options: -d /foo           Sets the root data directory to /foo.  Must be fully-qualified (ie. it must\n"
		"                           start with a /).  Default: %s.\n"
		"         -l <port>         Listen on the given port.  Default: %s.\n"
		"         -r <hostname>	Replicate files to the Verm instance running on <hostname>.\n"
		"            <hostname>:<port>          ... to the Verm instance running on <hostname> listening on <port>.\n"
		"                           This option may be used multiple times, to replicate to multiple servers concurrently.\n"
		"         -m <filename>     Load MIME content-types from the given file.  Default: %s.\n"
		"         -q                Quiet mode.  Don't print startup/shutdown/request log messages to stdout.\n",
		DEFAULT_ROOT, DEFAULT_HTTP_PORT, default_mime_types_file());
	return 100;
}

int ignore_pipe_signals() {
	struct sigaction sig;
	sigemptyset(&sig.sa_mask);
	sig.sa_handler = SIG_IGN;
	#ifdef SA_INTERRUPT
	sig.sa_flags = SA_INTERRUPT;  /* SunOS */
	#else
	sig.sa_flags = SA_RESTART;
	#endif
	if (sigaction(SIGPIPE, &sig, NULL) < 0) {
		perror("Couldn't install SIGPIPE handler");
		return -1;
	}
	return 0;
}

int wait_for_termination() {
	sigset_t signals;
	int _sig; /* currently unused, but can't pass NULL to sigwait on Linux */
	
	if (sigemptyset(&signals) < 0 ||
	    sigaddset(&signals, SIGQUIT) < 0 ||
	    sigaddset(&signals, SIGTERM) < 0 ||
	    sigaddset(&signals, SIGINT) < 0 ||
	    sigprocmask(SIG_BLOCK, &signals, NULL) < 0 ||
		sigwait(&signals, &_sig) < 0) {
		perror("Couldn't wait on the termination signals");
		return -1;
	}
	
	return 0;
}

int main(int argc, char* argv[]) {
	struct MHD_Daemon* http_daemon;
	int port = atoi(DEFAULT_HTTP_PORT);
	const char* mime_types_file = default_mime_types_file();
	int complain_about_mime_types = 0;
	struct Server server;
	server.quiet = 0;
	server.root_data_directory = DEFAULT_ROOT;
	
	int c;
	while ((c = getopt(argc, argv, "d:l:m:r:q")) != -1) {
		switch (c) {
			case 'd':
				if (strlen(optarg) <= 1 || *optarg != '/') return help();
				server.root_data_directory = optarg;
				break;
			
			case 'l':
				port = atoi(optarg);
				if (port <= 0) return help();
				break;
			
			case 'm':
				mime_types_file = optarg;
				complain_about_mime_types = 1;
				break;
			
			case 'q':
				server.quiet = 1;
				break;
			
			case 'r':
				// note that even if OUR listening port is changed from the default using -r, we still replicate to the default port on the other instance unless a port is explicitly given
				if (parse_and_add_replication_target(optarg)) return help();
				break;
			
			case '?':
				return help();
		}
	}
	
	if (!load_mime_types(mime_types_file) && complain_about_mime_types) {
		fprintf(stderr, "Couldn't load the MIME types file %s\n", mime_types_file);
		return help();
	}
	#ifdef DUMP_MIME_TYPES
	dump_mime_types();
	#endif

	if (ignore_pipe_signals()) return 6;
	
	http_daemon = MHD_start_daemon(
		MHD_USE_THREAD_PER_CONNECTION | EXTRA_DAEMON_FLAGS,
		port,
		NULL, NULL, // no connection address check
		&handle_request, &server,
		MHD_OPTION_NOTIFY_COMPLETED, &handle_request_completed, &server,
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) HTTP_TIMEOUT,
		MHD_OPTION_END);
	
	if (http_daemon == NULL) {
		fprintf(stderr, "Couldn't start HTTP daemon");
		return 1;
	}
	
	if (!server.quiet) fprintf(stdout, "Verm listening on http://localhost:%d/, data in %s\n", port, server.root_data_directory);
	if (wait_for_termination() < 0) return 6;

	MHD_stop_daemon(http_daemon);
	if (!server.quiet) fprintf(stdout, "Verm shutdown\n");
	return 0;
}
