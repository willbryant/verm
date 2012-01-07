#include "statistics_reports.h"

#include "response_logging.h"
#include "mhd_patches.h"

char* create_log_statistics_string(struct MHD_Connection *connection) {
	char* ret = NULL; // most OSs define asprintf as setting ret to NULL themselves if unable to allocate memory, but not all

	struct LogStatistics statistics;
	copy_log_statistics(&statistics);

	asprintf(&ret,
		"get_requests %ju\n"
		"get_requests_not_found %ju\n"
		"post_requests %ju\n"
		"post_requests_new_file_stored %ju\n"
		"post_requests_failed %ju\n"
		"put_requests %ju\n"
		"put_requests_new_file_stored %ju\n"
		"put_requests_failed %ju\n"
		"replication_push_attempts %ju\n"
		"replication_push_attempts_failed %ju\n"
		"connections_current %ju\n",
		(uintmax_t)statistics.get_requests, (uintmax_t)statistics.get_requests_not_found,
		(uintmax_t)statistics.post_requests, (uintmax_t)statistics.post_requests_new_file_stored, (uintmax_t)statistics.post_requests_failed,
		(uintmax_t)statistics.put_requests, (uintmax_t)statistics.put_requests_new_file_stored, (uintmax_t)statistics.put_requests_failed,
		(uintmax_t)statistics.replication_push_attempts, (uintmax_t)statistics.replication_push_attempts_failed,
		(uintmax_t)MHD_count_active_connections(connection)
	);
	return ret;
}
