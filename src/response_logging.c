#include "response_logging.h"

#include "src/daemon/internal.h"

#ifndef NEED_SA_LEN
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

int log_response(struct MHD_Connection* connection) {
	struct sockaddr* address;
	char addr[NI_MAXHOST];
	time_t now;
	struct tm now_tm;
	char timebuf[32];

	if (pthread_mutex_lock(&log_mutex) < 0) return -1;

	address = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS)->client_addr;
	if (getnameinfo(address, SA_LEN(address), addr, sizeof(addr), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV) < 0) { strncpy(addr, "-", sizeof(addr)); }
	time(&now);
	localtime_r(&now, &now_tm);
	strftime(timebuf, sizeof(timebuf), "%d/%b/%Y:%H:%M:%S %z", &now_tm); // standard CLF time format

	int result = fprintf(stdout, "%s - - [%s] \"%s %s %s\" %d %ju\n", addr, timebuf, connection->method, connection->url, connection->version, connection->responseCode, (uintmax_t)connection->response_write_position);

	if (pthread_mutex_unlock(&log_mutex) < 0) return -1;

	return result;
}
