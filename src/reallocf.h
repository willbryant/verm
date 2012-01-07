#ifndef REALLOCF_H
#define REALLOCF_H

#include <stdlib.h>

#ifdef NEED_REALLOCF
static inline void *reallocf(void *ptr, size_t size) {
	void *result = realloc(ptr, size);
	if (!result) free(ptr);
	return result;
}
#endif

#endif
