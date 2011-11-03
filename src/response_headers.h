#ifndef _RESPONSE_HEADERS_H
#define _RESPONSE_HEADERS_H

#include "platform.h"
#include "microhttpd.h"

int add_content_length(struct MHD_Response* response, size_t content_length);
int add_last_modified(struct MHD_Response* response, time_t last_modified);
int add_content_type(struct MHD_Response* response, const char* filename);
int add_gzip_content_encoding(struct MHD_Response* response);
int accept_gzip_encoding(struct MHD_Connection* connection);

#endif
