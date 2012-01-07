#include "str.h"

#include <string.h>
#include <stdlib.h>

int strendswith(const char *str, const char *ending) {
	const char *s = str + strlen(str) - strlen(ending);
	return (s >= str && strcmp(s, ending) == 0);
}

int boolean(const char *data, size_t size) {
	return (strncmp("0", data, size) != 0 &&
			strncasecmp("f", data, size) != 0 &&
			strncasecmp("false", data, size) != 0);
}

long atoi_or_default(const char* s, long def) {
	char* end = NULL;
	long result = strtol(s, &end, 10);
	if (end && *end == 0) return result;
	return def;
}

const char *strr2ndchr(const char *s, int c) {
	const char *curr = NULL, *prev = NULL;
	while (*s) {
		if (*s == c) {
			prev = curr;
			curr = s;
		}
		s++;
	}
	return prev;
}