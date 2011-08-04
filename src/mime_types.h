#ifndef _MIME_TYPES_H
#define _MIME_TYPES_H

const char* default_mime_types_file();
int load_mime_types(const char* filename);
void dump_mime_types();
const char *extension_for_mime_type(const char *mime_type);
const char *mime_type_for_extension(const char *extension);

#endif
