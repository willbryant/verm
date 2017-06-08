package main

import (
	"expvar"
	"fmt"
	"net/http"
)

type LogStatistics struct {
	GetRequests, GetRequestsFoundOnReplica, GetRequestsNotFound                            *expvar.Int
	PostRequests, PostRequestsNewFileStored, PostRequestsFailed                            *expvar.Int
	PutRequests, PutRequestsNewFileStored, PutRequestsMissingFileChecks, PutRequestsFailed *expvar.Int
	ReplicationPushAttempts, ReplicationPushAttemptsFailed                                 *expvar.Int
	ConnectionsCurrent                                                                     *expvar.Int
}

func NewLogStatistics() *LogStatistics {
	return &LogStatistics{
		GetRequests:                   expvar.NewInt("get_requests"),
		GetRequestsFoundOnReplica:     expvar.NewInt("get_requests_found_on_replica"),
		GetRequestsNotFound:           expvar.NewInt("get_requests_not_found"),
		PostRequests:                  expvar.NewInt("post_requests"),
		PostRequestsNewFileStored:     expvar.NewInt("post_requests_new_file_stored"),
		PostRequestsFailed:            expvar.NewInt("post_requests_failed"),
		PutRequests:                   expvar.NewInt("put_requests"),
		PutRequestsNewFileStored:      expvar.NewInt("put_requests_new_file_stored"),
		PutRequestsMissingFileChecks:  expvar.NewInt("put_requests_missing_file_checks"),
		PutRequestsFailed:             expvar.NewInt("put_requests_failed"),
		ReplicationPushAttempts:       expvar.NewInt("replication_push_attempts"),
		ReplicationPushAttemptsFailed: expvar.NewInt("replication_push_attempts_failed"),
		ConnectionsCurrent:            expvar.NewInt("connections_current"),
	}
}

func (server vermServer) serveStatistics(w http.ResponseWriter, req *http.Request, replicationTargets *ReplicationTargets) {
	w.WriteHeader(http.StatusOK)
	expvar.Do(func(kv expvar.KeyValue) {
		switch v := kv.Value.(type) {
		case *expvar.Int:
			fmt.Fprintf(w, "%s %d\n", kv.Key, v.Value())
		}
	})
	fmt.Fprintf(w, "%s", replicationTargets.StatisticsString())
}
