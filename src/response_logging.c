#include "response_logging.h"

#include "src/daemon/internal.h"

#ifdef NEED_SA_LEN
	#include <sys/un.h>

	static int __sa_len(sa_family_t af) {
		switch (af) {
			case AF_INET:
				return sizeof(struct sockaddr_in);
			
			case AF_INET6:
				return sizeof(struct sockaddr_in);
			
			case AF_LOCAL:
				return sizeof(struct sockaddr_un);
		}

		return 0;
	}

	#define SA_LEN(x) __sa_len(x->sa_family)
#else
	#define SA_LEN(x) x->sa_len
#endif

#define REQUEST_FAILED(connection) (connection->responseCode < 200 || connection->responseCode >= 400)

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct LogStatistics statistics;

int responded(struct MHD_Connection* connection) {
	return (connection->response != NULL);
}

int log_response(struct MHD_Connection* connection, int suppress_log_output, int statistics_request, int new_file_stored) {
	struct sockaddr* address;
	char addr[NI_MAXHOST];
	time_t now;
	struct tm now_tm;
	char timebuf[32];
	int result = 0;

	if (pthread_mutex_lock(&log_mutex) < 0) return -1;

	if (strcmp(connection->method, "GET") == 0) {
		if (!statistics_request) statistics.get_requests++;
		if (connection->responseCode == MHD_HTTP_NOT_FOUND) statistics.get_requests_not_found++;

	} else if (strcmp(connection->method, "POST") == 0) {
		statistics.post_requests++;
		if (new_file_stored) statistics.post_requests_new_file_stored++;
		if (REQUEST_FAILED(connection)) statistics.post_requests_failed++;

	} else if (strcmp(connection->method, "PUT") == 0) {
		statistics.put_requests++;
		if (new_file_stored) statistics.put_requests_new_file_stored++;
		if (REQUEST_FAILED(connection)) statistics.put_requests_failed++;
	}

	if (!suppress_log_output) {
		address = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr;
		if (getnameinfo(address, SA_LEN(address), addr, sizeof(addr), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV) < 0) { strncpy(addr, "-", sizeof(addr)); }
		time(&now);
		localtime_r(&now, &now_tm);
		strftime(timebuf, sizeof(timebuf), "%d/%b/%Y:%H:%M:%S %z", &now_tm); // standard CLF time format

		result = fprintf(stdout, "%s - - [%s] \"%s %s %s\" %d %ju\n", addr, timebuf, connection->method, connection->url, connection->version, connection->responseCode, (uintmax_t)connection->response_write_position);
	}

	if (pthread_mutex_unlock(&log_mutex) < 0) return -1;

	return result;
}

int copy_log_statistics(struct LogStatistics* dest) {
	if (pthread_mutex_lock(&log_mutex) < 0) return -1;

	memcpy(dest, &statistics, sizeof(statistics));
	
	if (pthread_mutex_unlock(&log_mutex) < 0) return -1;
	return 0;
}

char* create_log_statistics_string() {
	char* ret = NULL; // most OSs define asprintf as setting ret to NULL themselves if unable to allocate memory, but not all
	asprintf(&ret,
		"get_requests %ju\n"
		"get_requests_not_found %ju\n"
		"post_requests %ju\n"
		"post_requests_new_file_stored %ju\n"
		"post_requests_failed %ju\n"
		"put_requests %ju\n"
		"put_requests_new_file_stored %ju\n"
		"put_requests_failed %ju\n",
		(uintmax_t)statistics.get_requests, (uintmax_t)statistics.get_requests_not_found,
		(uintmax_t)statistics.post_requests, (uintmax_t)statistics.post_requests_new_file_stored, (uintmax_t)statistics.post_requests_failed,
		(uintmax_t)statistics.put_requests, (uintmax_t)statistics.put_requests_new_file_stored, (uintmax_t)statistics.put_requests_failed
	);
	return ret;
}
