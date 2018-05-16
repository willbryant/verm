package main

import (
	"expvar"
	"fmt"
	"net/http"
)

type LogStatistics struct {
	GetRequests, GetRequestsFoundOnReplica, GetRequestsNotFound                            PrometheusMetric
	PostRequests, PostRequestsNewFileStored, PostRequestsFailed                            PrometheusMetric
	PutRequests, PutRequestsNewFileStored, PutRequestsMissingFileChecks, PutRequestsFailed PrometheusMetric
	ReplicationPushAttempts, ReplicationPushAttemptsFailed                                 PrometheusMetric
	ConnectionsCurrent                                                                     PrometheusMetric
}

func NewLogStatistics() *LogStatistics {
	return &LogStatistics{
		GetRequests: NewPrometheusMetric(&promMetricOptions{
			name: "verm_get_requests_total",
			metricType: "counter",
			description: "GET requests served",
		}),
		GetRequestsFoundOnReplica: NewPrometheusMetric(&promMetricOptions{
			name: "verm_get_requests_found_on_replica_total",
			metricType: "counter",
			description: "GET requests found on replica",
		}),
		GetRequestsNotFound: NewPrometheusMetric(&promMetricOptions{
			name: "verm_get_requests_not_found_total",
			metricType: "counter",
			description: "GET requests not found",
		}),
		PostRequests: NewPrometheusMetric(&promMetricOptions{
			name: "verm_post_requests_total",
			metricType: "counter",
			description: "POST requests served",
		}),
		PostRequestsNewFileStored: NewPrometheusMetric(&promMetricOptions{
			name: "verm_post_requests_new_file_stored_total",
			metricType: "counter",
			description: "POST requests resulting in a new file stored",
		}),
		PostRequestsFailed: NewPrometheusMetric(&promMetricOptions{
			name: "verm_post_requests_failed_total",
			metricType: "counter",
			description: "POST requests failed",
		}),
		PutRequests: NewPrometheusMetric(&promMetricOptions{
			name: "verm_put_requests_total",
			metricType: "counter",
			description: "PUT requests served",
		}),
		PutRequestsNewFileStored: NewPrometheusMetric(&promMetricOptions{
			name: "verm_put_requests_new_file_stored_total",
			metricType: "counter",
			description: "PUT requests resulting in a new file stored",
		}),
		PutRequestsMissingFileChecks: NewPrometheusMetric(&promMetricOptions{
			name: "verm_put_requests_missing_file_checks_total",
			metricType: "counter",
			description: "PUT requests checking for missing files",
		}),
		PutRequestsFailed: NewPrometheusMetric(&promMetricOptions{
			name: "verm_put_requests_failed_total",
			metricType: "counter",
			description: "PUT requests failed",
		}),
		ReplicationPushAttempts: NewPrometheusMetric(&promMetricOptions{
			name: "verm_replication_push_attempts_total",
			metricType: "counter",
			description: "Replication push attempts",
		}),
		ReplicationPushAttemptsFailed: NewPrometheusMetric(&promMetricOptions{
			name: "verm_replication_push_attempts_failed_total",
			metricType: "counter",
			description: "Replication push attempts failed",
		}),
		ConnectionsCurrent: NewPrometheusMetric(&promMetricOptions{
			name: "verm_connections_current",
			metricType: "gauge",
			description: "HTTP connections",
		}),
	}
}

func (server vermServer) serveStatistics(w http.ResponseWriter, req *http.Request, replicationTargets *ReplicationTargets) {
	w.WriteHeader(http.StatusOK)
	server.Statistics.GetRequests.PrintStatistics(w)
	server.Statistics.GetRequestsFoundOnReplica.PrintStatistics(w)
	server.Statistics.GetRequestsNotFound.PrintStatistics(w)
	server.Statistics.PostRequests.PrintStatistics(w)
	server.Statistics.PostRequestsNewFileStored.PrintStatistics(w)
	server.Statistics.PostRequestsFailed.PrintStatistics(w)
	server.Statistics.PutRequests.PrintStatistics(w)
	server.Statistics.PutRequestsNewFileStored.PrintStatistics(w)
	server.Statistics.PutRequestsMissingFileChecks.PrintStatistics(w)
	server.Statistics.PutRequestsFailed.PrintStatistics(w)
	server.Statistics.ReplicationPushAttempts.PrintStatistics(w)
	server.Statistics.ReplicationPushAttemptsFailed.PrintStatistics(w)
	server.Statistics.ConnectionsCurrent.PrintStatistics(w)
	fmt.Fprintf(w, "%s", replicationTargets.StatisticsString())
}

type PrometheusMetric interface {
	Add(int64)
	PrintStatistics(w http.ResponseWriter)
}

type promMetricOptions struct {
	name 		string
	metricType 	string
	description string
}

type promMetric struct {
	metric		*expvar.Int
	options		*promMetricOptions
}

func NewPrometheusMetric(options *promMetricOptions) PrometheusMetric {
	return &promMetric{metric: expvar.NewInt(options.name), options: options}
}

func (pi *promMetric) PrintStatistics(w http.ResponseWriter) {
	fmt.Fprintf(w, "# HELP %s %s\n", pi.options.name, pi.options.description)
	fmt.Fprintf(w, "# TYPE %s %s\n", pi.options.name, pi.options.metricType)
	fmt.Fprintf(w, "%s %s\n", pi.options.name, pi.metric.String())
}

func (pi *promMetric) Add(val int64) {
	pi.metric.Add(val)
}