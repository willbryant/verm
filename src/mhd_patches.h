#ifndef MHD_PATCHES_H
#define MHD_PATCHES_H

#include "microhttpd.h"

const char *MHD_connection_get_http_version(struct MHD_Connection* connection);
const char *MHD_connection_get_method(struct MHD_Connection* connection);
const char *MHD_connection_get_url(struct MHD_Connection* connection);
int MHD_connection_has_response(struct MHD_Connection* connection);
unsigned int MHD_connection_get_response_code(struct MHD_Connection* connection);
uint64_t MHD_connection_get_response_bytes_sent(struct MHD_Connection* connection);
unsigned int MHD_count_active_connections(struct MHD_Connection *connection);

#endif
