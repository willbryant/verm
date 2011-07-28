#include "str.h"

#include <string.h>

int strendswith(const char *str, const char *ending) {
	const char *s = str + strlen(str) - strlen(ending);
	return (s >= str && strcmp(s, ending) == 0);
}

int boolean(const char *data, size_t size) {
	return (strncmp("0", data, size) != 0 &&
			strncasecmp("f", data, size) != 0 &&
			strncasecmp("false", data, size) != 0);
}
