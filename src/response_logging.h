#ifndef _RESPONSE_LOGGING_H
#define _RESPONSE_LOGGING_H

#include "platform.h"
#include "microhttpd.h"

struct LogStatistics {
	uint64_t get_requests, get_requests_not_found,
	         post_requests, post_requests_new_file_stored, post_requests_failed,
	         put_requests, put_requests_new_file_stored, put_requests_failed;
};

int responded(struct MHD_Connection* connection);
int log_response(struct MHD_Connection* connection, int suppress, int new_file_stored);
int copy_log_statistics(struct LogStatistics* dest);
char* create_log_statistics_string();

#endif
