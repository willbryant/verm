package verm

import "fmt"
import "net/http"

type LogStatistics struct {
	get_requests, get_requests_not_found,
	post_requests, post_requests_new_file_stored, post_requests_failed,
	put_requests, put_requests_new_file_stored, put_requests_failed,
	replication_push_attempts, replication_push_attempts_failed,
	connections_current uint64
}

func (server vermServer) serveStatistics(w http.ResponseWriter, req *http.Request, replication_targets *ReplicationTargets) {
	fmt.Fprintf(w,
		"get_requests %d\n"+
			"get_requests_not_found %d\n"+
			"post_requests %d\n"+
			"post_requests_new_file_stored %d\n"+
			"post_requests_failed %d\n"+
			"put_requests %d\n"+
			"put_requests_new_file_stored %d\n"+
			"put_requests_failed %d\n"+
			"replication_push_attempts %d\n"+
			"replication_push_attempts_failed %d\n"+
			"connections_current %d\n"+
			"%s",
		server.Statistics.get_requests,
		server.Statistics.get_requests_not_found,
		server.Statistics.post_requests,
		server.Statistics.post_requests_new_file_stored,
		server.Statistics.post_requests_failed,
		server.Statistics.put_requests,
		server.Statistics.put_requests_new_file_stored,
		server.Statistics.put_requests_failed,
		server.Statistics.replication_push_attempts,
		server.Statistics.replication_push_attempts_failed,
		server.Statistics.connections_current,
		replication_targets.StatisticsString())
}
