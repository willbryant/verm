#define DEFAULT_HTTP_PORT 1138
#define HTTP_TIMEOUT 60
#define POST_BUFFER_SIZE 65536
#define MAX_DIRECTORY_LENGTH 256
#define MAX_PATH_LENGTH 512 // checked; enough for /data root directory:200/client-requested directory:256/hashed filename:44.extension:8
#define DIRECTORY_PERMISSION 0777
#undef  DEBUG
#undef  DUMP_MIME_TYPES

#define DEFAULT_ROOT "/var/lib/verm"
#define DIRECTORY_IF_NOT_GIVEN_BY_CLIENT "/default" // rather than letting people upload directly into the root directory, which in practice is a PITA to administer.  no command-line option for this because it should be provided by the client, so letting admins change it implies mis-use by the client which would be a problem down the track.

#include "platform.h"
#include "microhttpd.h"
#include <openssl/sha.h>
#include "responses.h"
#include "decompression.h"
#include "str.h"
#include "mime_types.h"

#ifdef DEBUG
	#define EXTRA_DAEMON_FLAGS MHD_USE_DEBUG
	#define DEBUG_PRINT(...) fprintf(stdout, __VA_ARGS__)
#else
	#define EXTRA_DAEMON_FLAGS 0
	#define DEBUG_PRINT(...) 
#endif

struct Options {
	char* root_data_directory;
};

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

int add_gzip_content_encoding(struct MHD_Response* response) {
	return MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_ENCODING, "gzip");
}

int accept_gzip_encoding(struct MHD_Connection* connection) {
	const char* value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_ACCEPT_ENCODING);
	if (!value) return 1; // spec says we should assume any of the "common" encodings are supported - ie. gzip and compress - if not explicitly told

	while (1) {
		while (isspace(*value)) value++;
		if (*value == '*' || strncmp(value, "gzip", 4) == 0 || strncmp(value, "x-gzip", 6) == 0) { // spec requests we treat x-gzip as gzip
			value += (*value == '*' ? 1 : (*value == 'x' ? 6 : 4));
			while (isspace(*value)) value++;
			
			if (*value == ',') return 1; // no q-value given, so it's acceptable
			if (*value++ == ';') {
				while (isspace(*value)) value++; if (*value++ != 'q') return 0; // syntax error
				while (isspace(*value)) value++; if (*value++ != '=') return 0; // syntax error
				while (isspace(*value)) value++;
				return (atof(value) != 0.0); // acceptable unless q-value 0 given; ok to treat invalid floating-point numbers as 0 here, since that falls back to the safe case of returning 0 which is what we'd return for such a syntax error anyway
			}
			// so the encoding name started with * or gzip, but that wasn't all of it, so no match; carry on and try the next
		}
		
		while (*value && *value != ',') value++;
		if (!*value++) return 0;
	}
}

int handle_get_or_head_request(
	struct Options* daemon_options, struct MHD_Connection* connection,
    const char* path, void** _request_data, int send_data) {

	int fd;
	struct stat st;
	struct MHD_Response* response;
	int ret;
	char fs_path[MAX_PATH_LENGTH];
	const char* request_value;
	int want_decompressed = 0;

	if (strcmp(path, "/") == 0) {
		return send_upload_page_response(connection);
	}
	
	// check and expand the path (although the MHD docs use 'url' as the name for this parameter, it's actually the path - it does not include the scheme/hostname/query, and has been URL-decoded)
	if (path[0] != '/' || strstr(path, "/..") ||
	    snprintf(fs_path, sizeof(fs_path), "%s%s", daemon_options->root_data_directory, path) >= sizeof(fs_path)) {
		return send_file_not_found_response(connection);
	}
	
	DEBUG_PRINT("trying to open %s\n", fs_path);
	do { fd = open(fs_path, O_RDONLY); } while (fd < 0 && errno == EINTR);
	if (fd < 0) {
		switch (errno) {
			case ENOENT:
			case EACCES:
				if (strendswith(fs_path, ".gz") || // if the client asked for a .gz, don't try .gz.gz
					snprintf(fs_path, sizeof(fs_path), "%s%s.gz", daemon_options->root_data_directory, path) >= sizeof(fs_path)) {
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
					
					want_decompressed = 1;
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
	
	if ((request_value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_NONE_MATCH)) &&
	    strcmp(request_value, path + 1) == 0) {
		DEBUG_PRINT("%s not modified\n", path);
		return send_not_modified_response(connection, path + 1); // to match the ETag we issue below
	}
	
	// FUTURE: support range requests
	if (send_data) {
		// ie. a GET request
		if (want_decompressed && !accept_gzip_encoding(connection)) {
			// the file is compressed, but the client explicitly told us they don't support that, so decompress the file
			void* decompression = create_decompressor(fd);
			if (!decompression) return MHD_NO; // out of memory
			response = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, DECOMPRESSION_CHUNK, &decompress_chunk, decompression, &destroy_decompressor);
		} else {
			// the file is either not compressed, or is compressed and the client supports that, so we can serve the file as-is
			response = MHD_create_response_from_fd_at_offset(st.st_size, fd, 0); // fd will be closed by MHD when the response is destroyed
			
			// if the file is compressed, then we need to add a header saying so
			if (want_decompressed && !add_gzip_content_encoding(response)) return MHD_NO; // out of memory
			want_decompressed = 0;
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
	ret = (want_decompressed ? 1 : add_content_length(response, st.st_size)) &&
	      add_last_modified(response, st.st_mtime) &&
	      add_content_type(response, path) &&
	      MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, path + 1) && // since the path includes the hash, it's a perfect ETag
	      MHD_add_response_header(response, MHD_HTTP_HEADER_EXPIRES, "Tue, 19 Jan 2038 00:00:00"), // essentially never expires
	      MHD_queue_response(connection, MHD_HTTP_OK, response); // does nothing and returns our desired MHD_NO if response is NULL
	MHD_destroy_response(response); // does nothing if response is NULL
	return ret;
}

int handle_post_data(
	void *post_data, enum MHD_ValueKind kind, const char *key, const char *filename,
	const char *content_type, const char *transfer_encoding,
	const char *data, uint64_t offset, size_t size) {

	struct Upload* upload = (struct Upload*) post_data;
	
	if (strcmp(key, "uploaded_file") == 0) {
		DEBUG_PRINT("uploading into %s: %s, %s, %s, %s (%llu, %ld)\n", upload->tempfile_fs_path, key, filename, content_type, transfer_encoding, offset, size);
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
			DEBUG_PRINT("Looking up extension for %s\n", content_type);
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
	DEBUG_PRINT("freeing upload object\n", NULL);
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

void _try_make_tempfile(struct Upload* upload, const char* root_data_directory) {
	/* annoyingly, glibc's implementation of mkstemp overwrites the XXXXXXXX part of the template before attempting to
	   open the directory.  consequently, subsequent calls to mkstemp would fail on Linux if we did not re-sprintf the
	   template string, so we have extracted these two lines out to this method to avoid needing to duplicate the
	   sprintf in the two places it's used below. */
	snprintf(upload->tempfile_fs_path, sizeof(upload->tempfile_fs_path), "%s%s/upload.XXXXXXXX", root_data_directory, upload->directory);
	do { upload->tempfile_fd = mkstemp(upload->tempfile_fs_path); } while (upload->tempfile_fd == -1 && errno == EINTR);
}

struct Upload* create_upload(struct MHD_Connection *connection, const char* root_data_directory, const char *path) {
	char* s;
	
	if (path[0] != '/' || strstr(path, "/..") || strlen(path) >= MAX_DIRECTORY_LENGTH) {
		fprintf(stderr, "Refusing post to a suspicious path: '%s'\n", path);
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
		strncpy(upload->directory, DIRECTORY_IF_NOT_GIVEN_BY_CLIENT, sizeof(upload->directory));
	}
	
	SHA256_Init(&upload->hasher);
	
	upload->pp = MHD_create_post_processor(connection, POST_BUFFER_SIZE, &handle_post_data, upload);
	if (!upload->pp) { // presumably out of memory
		fprintf(stderr, "Couldn't create a post processor! (out of memory?)\n");
		free_upload(upload);
		return NULL;
	}
	
	_try_make_tempfile(upload, root_data_directory);
	
	if (upload->tempfile_fd < 0 && errno == ENOENT) {
		// create the directory (or directories, if nested)
		char *sep;
		int ret;
		for (sep = upload->tempfile_fs_path + strlen(root_data_directory); sep; sep = strchr(sep + 1, '/')) {
			*sep = 0;
			do { ret = mkdir(upload->tempfile_fs_path, DIRECTORY_PERMISSION); } while (ret < 0 && errno == EINTR);
			if (ret != 0 && errno != EEXIST) { // EEXIST would just mean another process beat us to it
				fprintf(stderr, "Couldn't create %s: %s (%d)\n", upload->tempfile_fs_path, strerror(errno), errno);
				return NULL;
			}
			*sep = '/';
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

int link_file(struct Upload* upload, const char* root_data_directory, char* encoded) {
	int ret;
	int attempt = 1;
	struct stat st;
	char final_directory[MAX_PATH_LENGTH];
	int created_directory = 0;
	
	// we put each file in a subdirectory off the main root, whose name is the first two characters of the hash.
	// we don't repeat those characters in the filename.
	ret = snprintf(final_directory, sizeof(final_directory), "%s%s/%.2s", root_data_directory, upload->directory, encoded);
	if (ret >= sizeof(final_directory)) { // shouldn't be possible unless misconfigured
		fprintf(stderr, "Couldn't generate directory for %s under %s%s within limits\n", encoded, root_data_directory, upload->directory);
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
			fprintf(stderr, "Couldn't generate filename for %s under %s within limits\n", upload->tempfile_fs_path, root_data_directory);
			return -1;
		}
		
		DEBUG_PRINT("trying to link as %s\n", upload->final_fs_path);
		do { ret = link(upload->tempfile_fs_path, upload->final_fs_path); } while (ret < 0 && errno == EINTR);
		if (ret == 0) break; // successfully linked
		
		if (errno != EEXIST) {
			if (errno == ENOENT && !created_directory) {
				DEBUG_PRINT("creating directory %s\n", final_directory);
				do { ret = mkdir(final_directory, DIRECTORY_PERMISSION); } while (ret < 0 && errno == EINTR);
				if (ret != 0 && errno != EEXIST) { // EEXIST would just mean another process beat us to it
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

int complete_upload(struct Upload* upload, const char* root_data_directory) {
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

	DEBUG_PRINT("hashed, encoded filename is %s\n", encoded);
	return link_file(upload, root_data_directory, encoded);
}

int handle_post_request(
	struct Options* daemon_options, struct MHD_Connection* connection,
    const char *path,
    const char *upload_data, size_t *upload_data_size,
	void **request_data) {
	
	DEBUG_PRINT("handle_post_request to %s with %ld bytes, request_data set %d, upload_data set %d\n", path, *upload_data_size, (*request_data ? 1 : 0), (upload_data ? 1 : 0));

	if (!*request_data) { // new connection
		*request_data = create_upload(connection, daemon_options->root_data_directory, path);
		return *request_data ? MHD_YES : MHD_NO;
	}
	
	struct Upload* upload = (struct Upload*) *request_data;
	if (*upload_data_size > 0) {
	 	return process_upload_data(upload, upload_data, upload_data_size);
	} else {
		DEBUG_PRINT("completing upload\n", NULL);
		if (complete_upload(upload, daemon_options->root_data_directory) < 0) {
			DEBUG_PRINT("completing failed\n", NULL);
			return MHD_NO;
		} else {
			char* final_relative_path = upload->final_fs_path + strlen(daemon_options->root_data_directory);
			if (upload->redirect_afterwards) {
				DEBUG_PRINT("redirecting to %s\n", final_relative_path);
				return send_redirected_response(connection, final_relative_path);
			} else {
				DEBUG_PRINT("created %s\n", final_relative_path);
				return send_see_other_response(connection, final_relative_path);
			}
		}
	}
}
		
int handle_request(
	void* void_daemon_options, struct MHD_Connection* connection,
    const char* path, const char* method, const char* version,
    const char* upload_data, size_t* upload_data_size,
	void** request_data) {
	struct Options* daemon_options = (struct Options*) void_daemon_options;
	
	if (strcmp(method, "GET") == 0) {
		return handle_get_or_head_request(daemon_options, connection, path, request_data, 1);
		
	} else if (strcmp(method, "HEAD") == 0) {
		return handle_get_or_head_request(daemon_options, connection, path, request_data, 0);
		
	} else if (strcmp(method, "POST") == 0) {
		return handle_post_request(daemon_options, connection, path, upload_data, upload_data_size, request_data);
		
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

int help() {
	fprintf(stderr, "%s",
		"Usage: verm\n"
		"          - Runs verm.\n"
		"\n"
		"            verm requires no privileges except read/write access to the data directory, and should\n"
		"            be run as the user you want to own the files.\n"
		"            It can be run as root, but running your daemons as root is generally discouraged.\n"
		"\n"
		"Options: -d /foo           Changes the root data directory to /foo.  Must be fully-qualified (ie. it must\n"
		"                           start with a /).  Default: %s.\n"
		"         -l <port>         Listen on the given port.  Default: %d.\n"
		"         -m <filename>     Load MIME content-types from the given file.  Default: %s.\n"
		"         -q                Quiet mode.  Don't print startup and shutdown messages to stdout.\n",
		DEFAULT_ROOT, DEFAULT_HTTP_PORT, default_mime_types_file());
	return 100;
}

int wait_for_termination() {
	sigset_t signals;
	int _sig; /* currently unused, but can't pass NULL to sigwait on Linux */
	
	if (sigemptyset(&signals) < 0 ||
	    sigaddset(&signals, SIGQUIT) < 0 ||
	    sigaddset(&signals, SIGTERM) < 0 ||
	    sigaddset(&signals, SIGINT) < 0 ||
		sigwait(&signals, &_sig) < 0) {
		perror("Couldn't wait on the termination signals");
		return -1;
	}
	
	return 0;
}

int main(int argc, char* argv[]) {
	struct MHD_Daemon* daemon;
	int port = DEFAULT_HTTP_PORT;
	const char* mime_types_file = default_mime_types_file();
	int complain_about_mime_types = 0;
	int quiet = 0;
	struct Options daemon_options;
	daemon_options.root_data_directory = DEFAULT_ROOT;
	
	int c;
	while ((c = getopt(argc, argv, "d:l:m:q")) != -1) {
		switch (c) {
			case 'd':
				if (strlen(optarg) <= 1 || *optarg != '/') return help();
				daemon_options.root_data_directory = optarg;
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
				quiet = 1;
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
	
	daemon = MHD_start_daemon(
		MHD_USE_THREAD_PER_CONNECTION | EXTRA_DAEMON_FLAGS,
		port,
		NULL, NULL, // no connection address check
		&handle_request, &daemon_options,
		MHD_OPTION_NOTIFY_COMPLETED, &handle_request_completed, NULL, // no extra argument to handle_request
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) HTTP_TIMEOUT,
		MHD_OPTION_END);
	
	if (daemon == NULL) {
		fprintf(stderr, "Couldn't start HTTP daemon");
		return 1;
	}
	
	if (!quiet) fprintf(stdout, "Verm listening on http://localhost:%d/, data in %s\n", port, daemon_options.root_data_directory);
	if (wait_for_termination() < 0) return 6;

	MHD_stop_daemon(daemon);
	if (!quiet) fprintf(stdout, "Verm shutdown\n");
	return 0;
}
