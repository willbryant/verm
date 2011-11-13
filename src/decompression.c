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

void *create_memory_decompressor() {
	int ret;
	z_stream *strm = malloc(sizeof(z_stream));

	if (!strm) {
		fprintf(stderr, "Unable to allocate gzip decompression stream\n");
		return NULL;
	}

	strm->zalloc = Z_NULL;
	strm->zfree = Z_NULL;
	strm->opaque = Z_NULL;
	strm->avail_in = 0;
	strm->next_in = Z_NULL;
	ret = inflateInit2(strm, 16 + 15); // the magic number 16 means 'decode gzip format (and not zlib format)'; 15 is the chosen window size, which we have made the same as inflateInit() uses
	if (ret != Z_OK) {
		fprintf(stderr, "Unable to initialize gzip decompression stream (%d)\n", ret);
		free(strm);
		return NULL;
	}

	return strm;
}

void destroy_memory_decompressor(void *decompression_info) {
	z_stream *strm = (z_stream *)decompression_info;
	inflateEnd(strm);
	free(strm);
}

ssize_t decompress_memory_chunk(void *decompression_info, const char **in_buf, size_t *in_size, char *out_buf, size_t out_size) {
	z_stream *strm = (z_stream *)decompression_info;
	int ret;

	strm->next_in = (void*) *in_buf;
	strm->avail_in = *in_size;
	strm->next_out = out_buf;
	strm->avail_out = out_size;

	ret = inflate(strm, Z_SYNC_FLUSH);
	switch (ret) {
		case Z_STREAM_ERROR:
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			fprintf(stderr, "Stream decompression error (%d)\n", ret);
			return -1;
	}

	*in_buf = strm->next_in;
	*in_size = strm->avail_in;
	return out_size - strm->avail_out;
}
