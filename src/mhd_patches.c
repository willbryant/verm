#include "mhd_patches.h"

#include "src/daemon/internal.h"

const char *MHD_connection_get_http_version(struct MHD_Connection* connection) {
	return connection->version;
}

const char *MHD_connection_get_method(struct MHD_Connection* connection) {
	return connection->method;
}

const char *MHD_connection_get_url(struct MHD_Connection* connection) {
	return connection->url;
}

int MHD_connection_has_response(struct MHD_Connection* connection) {
	return (connection->response != NULL);
}

unsigned int MHD_connection_get_response_code(struct MHD_Connection* connection) {
	return connection->responseCode;
}

uint64_t MHD_connection_get_response_bytes_sent(struct MHD_Connection* connection) {
	return connection->response_write_position;
}
