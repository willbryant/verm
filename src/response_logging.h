#ifndef _RESPONSE_LOGGING_H
#define _RESPONSE_LOGGING_H

#include "platform.h"
#include "microhttpd.h"

struct LogStatistics {
	uint64_t get_requests, get_requests_not_found,
	         post_requests, post_requests_new_file_stored, post_requests_failed,
	         put_requests, put_requests_new_file_stored, put_requests_failed,
	         replication_push_attempts, replication_push_attempts_failed;
};

int log_response(struct MHD_Connection* connection, int suppress_log_output, int statistics_request, int new_file_stored);
int log_replication_statistic(int successful);
int copy_log_statistics(struct LogStatistics* dest);

#endif
