#include "response_logging.h"

#include "mhd_patches.h"

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

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct LogStatistics statistics;

int responded(struct MHD_Connection* connection) {
	return MHD_connection_has_response(connection);
}

int log_response(struct MHD_Connection* connection, int suppress_log_output, int statistics_request, int new_file_stored) {
	struct sockaddr* address;
	char addr[NI_MAXHOST];
	time_t now;
	struct tm now_tm;
	char timebuf[32];
	int result = 0;

	unsigned int responseCode = MHD_connection_get_response_code(connection);
	const char* method = MHD_connection_get_method(connection);

	if (pthread_mutex_lock(&log_mutex) < 0) return -1;

	if (strcmp(method, "GET") == 0) {
		if (!statistics_request) statistics.get_requests++;
		if (responseCode == MHD_HTTP_NOT_FOUND) statistics.get_requests_not_found++;

	} else if (strcmp(method, "POST") == 0) {
		statistics.post_requests++;
		if (new_file_stored) statistics.post_requests_new_file_stored++;
		if (responseCode < 200 || responseCode >= 400) statistics.post_requests_failed++;

	} else if (strcmp(method, "PUT") == 0) {
		statistics.put_requests++;
		if (new_file_stored) statistics.put_requests_new_file_stored++;
		if (responseCode < 200 || responseCode >= 400) statistics.put_requests_failed++;
	}

	if (!suppress_log_output) {
		address = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr;
		if (getnameinfo(address, SA_LEN(address), addr, sizeof(addr), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV) < 0) { strncpy(addr, "-", sizeof(addr)); }
		time(&now);
		localtime_r(&now, &now_tm);
		strftime(timebuf, sizeof(timebuf), "%d/%b/%Y:%H:%M:%S %z", &now_tm); // standard CLF time format

		result = fprintf(stdout, "%s - - [%s] \"%s %s %s\" %u %ju\n",
			addr,
			timebuf,
			MHD_connection_get_method(connection),
			MHD_connection_get_url(connection),
			MHD_connection_get_http_version(connection),
			MHD_connection_get_response_code(connection),
			(uintmax_t)MHD_connection_get_response_bytes_sent(connection));
	}

	if (pthread_mutex_unlock(&log_mutex) < 0) return -1;

	return result;
}

int log_replication_statistic(int successful) {
	if (pthread_mutex_lock(&log_mutex) < 0) return -1;
	statistics.replication_push_attempts++;
	if (!successful) statistics.replication_push_attempts_failed++;
	if (pthread_mutex_unlock(&log_mutex) < 0) return -1;
	return 0;
}

int copy_log_statistics(struct LogStatistics* dest) {
	if (pthread_mutex_lock(&log_mutex) < 0) return -1;

	memcpy(dest, &statistics, sizeof(statistics));
	
	if (pthread_mutex_unlock(&log_mutex) < 0) return -1;
	return 0;
}
