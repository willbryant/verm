#ifndef _DECOMPRESSION_H
#define _DECOMPRESSION_H

#include "platform.h"

#define DECOMPRESSION_CHUNK 16384

void* create_decompressor(int fd);
void destroy_decompressor(void* decompression_info);
ssize_t decompress_chunk(void* decompression_info, uint64_t pos, char* buf, size_t max);

#endif