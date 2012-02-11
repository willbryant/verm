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

unsigned int MHD_count_active_connections(struct MHD_Connection *connection) {
	struct MHD_Daemon *daemon = connection->daemon;
	unsigned int result = 0;
	if (pthread_mutex_lock(&daemon->cleanup_connection_mutex) != 0) return 0;
	connection = daemon->connections_head;
	while (connection) { connection = connection->next; result++; }
	if (pthread_mutex_unlock(&daemon->cleanup_connection_mutex) != 0) return 0;
	return result;
}
