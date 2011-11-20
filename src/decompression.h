#ifndef _DECOMPRESSION_H
#define _DECOMPRESSION_H

#include "platform.h"

#define DECOMPRESSION_CHUNK 16384

void *create_file_decompressor(int fd);
off_t get_decompressed_file_size(int fd, off_t file_size);
void destroy_file_decompressor(void *decompression_info);
ssize_t decompress_file_chunk(void *decompression_info, uint64_t pos, char *buf, size_t max);

void *create_memory_decompressor();
void destroy_memory_decompressor(void *decompression_info);
ssize_t decompress_memory_chunk(void *decompression_info, const char **in_buf, size_t *in_size, char *out_buf, size_t out_size);

#endif