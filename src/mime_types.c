#include "mime_types.h"

#include <stdio.h>
#include <string.h>
#include "khash.h"

KHASH_MAP_INIT_STR(str, char*)

static khash_t(str) *by_mime_type;
static khash_t(str) *by_extension;

struct BuiltinType {
	const char *mime_type;
	const char *extension;
};

static struct BuiltinType builtin_mime_types[] = {
	/* a somewhat-arbitrary selection of the most important standard mime types for use with net apps.
	   excludes all non-standard or vendor-specific types and most non-document types.  should generally
	   be supplemented by your /etc/mime.types file, especially if you plan to store audio, video, or
	   animation files or source documents from word processors & spreadsheets etc. */
	{"text/plain",                  "txt"},
	{"text/html",                   "htm"},
	{"text/html",                   "html"},
	{"text/xml",                    "xsl"},
	{"text/xml",                    "xsd"},
	{"text/xml",                    "xml"},
	{"text/css",                    "css"},
	{"text/comma-separated-values", "csv"},
	{"text/csv",                    "csv"}, // later entries overwrite earlier entries for the same extension
	{"text/tab-separated-values",   "tsv"},
	{"image/jpeg",                  "jpeg"},
	{"image/jpeg",                  "jpg"}, // later entries overwrite earlier entries for the same mime type
	{"image/gif",                   "gif"},
	{"image/png",                   "png"},
	{"image/svg+xml",               "svg"},
	{"application/pdf",             "pdf"},
	{"application/javascript",      "js"},
	{"application/json",            "json"},
	{"application/tar",             "tar"},
	{"application/xhtml+xml",       "xhtml"},
	{"application/zip",             "zip"},
	{"message/rfc822",              "eml"},
};

void load_builtin_mime_types() {
	khiter_t k;
	int ret;
	struct BuiltinType *curr;
	
	struct BuiltinType *end = builtin_mime_types + sizeof(builtin_mime_types)/sizeof(struct BuiltinType);
	for (curr = builtin_mime_types; curr < end; curr++) {
		k = kh_put(str, by_mime_type, curr->mime_type, &ret);
		if (!ret) free(kh_val(by_mime_type, k));
		kh_val(by_mime_type, k) = strdup(curr->extension);

		k = kh_put(str, by_extension, curr->extension, &ret);
		if (!ret) free(kh_val(by_extension, k));
		kh_val(by_extension, k) = strdup(curr->mime_type);
	}
}

int load_mime_types_from_file(const char *filename) {
	FILE *file;
	char buf[1024];
	char *tok;
	char *mime_type;
	char *lasts;
	khiter_t k;
	int ret;
	
	file = fopen(filename, "r");
	if (file) {
		while (fgets(buf, sizeof(buf), file)) {
			mime_type = strtok_r(buf, " \t", &lasts);
			if (mime_type[0] == '#') continue;
			tok = strtok_r(NULL, " \t\n", &lasts);
			
			if (tok && strcmp(mime_type, "application/octet-stream") != 0) { // ignore generic extensions
				// register the first extension as the default for this mime type
				char *dup_mime_type = strdup(mime_type);
				k = kh_put(str, by_mime_type, dup_mime_type, &ret);
				if (!ret) {
					free(dup_mime_type); // key was already in the hash, so we don't need another copy
					free(kh_value(by_mime_type, k)); // but our value will replace the old one, free that
				}
				kh_val(by_mime_type, k) = strdup(tok);
			}
			
			while (tok) {
				if (strlen(tok) > 0) {
					char *dup_tok = strdup(tok);
					k = kh_put(str, by_extension, dup_tok, &ret);
					if (!ret) {
						free(dup_tok); // key was already in the hash, so we don't need another copy
						free(kh_value(by_extension, k)); // but our value will replace the old one, free that
					}
					kh_val(by_extension, k) = strdup(mime_type);
				}
				
				tok = strtok_r(NULL, " \t\n", &lasts);
			}
		}
		fclose(file);
		return 1;
	} else {
		return 0;
	}
}

void dump_mime_types() {
	khiter_t k;

	for (k = kh_begin(by_mime_type); k != kh_end(by_mime_type); ++k) {
		if (kh_exist(by_mime_type, k)) fprintf(stderr, "mime type %s will be stored with extension %s\n", kh_key(by_mime_type, k), kh_value(by_mime_type, k));
	}

	for (k = kh_begin(by_extension); k != kh_end(by_extension); ++k) {
		if (kh_exist(by_extension, k)) fprintf(stderr, "extension %s will be served with content-type %s\n", kh_key(by_extension, k), kh_value(by_extension, k));
	}
}

int load_mime_types(const char* filename) {
	by_mime_type = kh_init(str);
	by_extension = kh_init(str);

	load_builtin_mime_types();
	return load_mime_types_from_file(filename);
}

const char* default_mime_types_file() {
	return "/etc/mime.types";
}

const char *extension_for_mime_type(const char *mime_type) {
	khiter_t k;

	if (!mime_type) return NULL;
	k = kh_get(str, by_mime_type, mime_type);
	if (k == kh_end(by_mime_type)) return NULL;
	return kh_value(by_mime_type, k);
}

const char *mime_type_for_extension(const char *extension) {
	khiter_t k;

	if (!extension) return NULL;
	k = kh_get(str, by_extension, extension);
	if (k == kh_end(by_extension)) return NULL;
	return kh_value(by_extension, k);
}
