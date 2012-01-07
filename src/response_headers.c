#include "response_headers.h"

#include <ctype.h> /* for isspace */
#include "mime_types.h"

static int add_response_header(struct MHD_Response* response, const char *header, const char *content) {
	int result = MHD_add_response_header(response, header, content);
	if (result == MHD_NO) fprintf(stderr, "Couldn't add response header (out of memory?)\n");
	return result;
}

int add_content_length(struct MHD_Response* response, size_t content_length) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%ju", (uintmax_t)content_length);
	return add_response_header(response, MHD_HTTP_HEADER_CONTENT_LENGTH, buf);
}

int add_last_modified(struct MHD_Response* response, time_t last_modified) {
	char buf[64];
	struct tm t;
	gmtime_r(&last_modified, &t);
	strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &t);
	return add_response_header(response, MHD_HTTP_HEADER_LAST_MODIFIED, buf);
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
		found = mime_type_for_extension(found);
		if (found) return add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, found);
	}

	// no extension or no corresponding mime type definition, don't add a content-type header but carry on
	return MHD_YES;
}

int add_gzip_content_encoding(struct MHD_Response* response) {
	return add_response_header(response, MHD_HTTP_HEADER_CONTENT_ENCODING, "gzip");
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
