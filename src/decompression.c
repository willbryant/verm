#include "decompression.h"
#include "microhttpd.h"
#include <zlib.h>

void *create_file_decompressor(int fd) {
	gzFile gz_file = gzdopen(fd, "rb");
	
    if (!gz_file) {
		fprintf(stderr, "Unable to allocate gzip decompressor\n");
        return NULL;
	}
	
	return gz_file;
}

void destroy_file_decompressor(void *decompression_info) {
	gzFile gz_file = (gzFile) decompression_info;
	gzclose(gz_file); // also closes the original file descriptor
}

ssize_t decompress_file_chunk(void *decompression_info, uint64_t pos, char *buf, size_t max) {
	gzFile gz_file = (gzFile) decompression_info;
	int result = gzread(gz_file, buf, max);
	if (result < 0) {
		fprintf(stderr, "Error decompressing: %s\n", gzerror(gz_file, NULL));
		return MHD_CONTENT_READER_END_WITH_ERROR;
	}
	if (result == 0) {
		return MHD_CONTENT_READER_END_OF_STREAM;
	}
	return result;
}
