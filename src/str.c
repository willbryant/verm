#include "str.h"

#include <string.h>
#include <strings.h>
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

char *astrcat(char *s1, char *s2) {
	char* ret = NULL; // most OSs define asprintf as setting ret to NULL themselves if unable to allocate memory, but not all
	asprintf(&ret, "%s%s", s1 ? s1 : "", s2 ? s2 : "");
	return ret;
}

char *astrcat_and_free_args(char *s1, char *s2) {
	char* result = astrcat(s1, s2);
	free(s1);
	free(s2);
	return result;
}
