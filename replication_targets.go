package main

import "fmt"
import "strings"

type ReplicationTargets struct {
	targets []*ReplicationTarget
}

func parseTarget(value string) (string, string) {
	parts := strings.Split(value, ":")

	if len(parts) > 1 {
		return parts[0], parts[1]
	} else {
		return parts[0], DefaultPort
	}
}

func (targets *ReplicationTargets) Set(value string) error {
	for _, s := range strings.Split(value, ",") {
		hostname, port := parseTarget(s)
		target := NewReplicationTarget(hostname, port)
		targets.targets = append(targets.targets, &target)
	}
	return nil
}

func (targets *ReplicationTargets) String() string {
	// shown as the default in the help text
	return "<hostname> or <hostname>:<port>"
}

func (targets *ReplicationTargets) Start(rootDataDirectory string, statistics *LogStatistics, workers int) {
	for _, target := range targets.targets {
		target.Start(rootDataDirectory, statistics, workers)
	}
}

func (targets *ReplicationTargets) EnqueueFile(location string, replicating bool) {
	for _, target := range targets.targets {
		if replicating {
			target.enqueueReplicatedFile(location)
		} else {
			target.enqueueNewFile(location)
		}
	}
}

func (targets *ReplicationTargets) EnqueueResync() {
	for _, target := range targets.targets {
		target.enqueueResync()
	}
}

func (targets *ReplicationTargets) StatisticsString() string {
	if len(targets.targets) <= 0 {
		return ""
	}
	metricName := "verm_replication_queue_length"
	result := fmt.Sprintf("# HELP %s Number of files in the queue to be replicated to each configured replica.\n", metricName)
	result = fmt.Sprintf("%s# TYPE %s gauge\n", result, metricName)
	for _, target := range targets.targets {
		result = fmt.Sprintf(
			"%s%s{target=\"%s:%s\"} %d\n",
			result, metricName,
			target.hostname, target.port, target.queueLength())
	}
	return result
}
