package verm

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
		return parts[0], DEFAULT_VERM_PORT
	}
}

func (targets *ReplicationTargets) Set(value string) error {
	hostname, port := parseTarget(value)
	target := ReplicationTarget{
		hostname: hostname,
		port:     port,
		jobs:     make(chan string, REPLICATION_BACKLOG),
		resync:   make(chan struct{}, 1),
	}
	targets.targets = append(targets.targets, &target)
	return nil
}

func (targets *ReplicationTargets) String() string {
	// shown as the default in the help text
	return "<hostname> or <hostname>:<port>"
}

func (targets *ReplicationTargets) Start(root_data_directory string, statistics *LogStatistics) {
	for _, target := range targets.targets {
		target.root_data_directory = root_data_directory
		target.statistics = statistics
		go target.replicateFromQueue()
		go target.resyncFromQueue()
	}
}

func (targets *ReplicationTargets) EnqueueJob(job string) {
	for _, target := range targets.targets {
		target.enqueueJob(job)
	}
}

func (targets *ReplicationTargets) EnqueueResync() {
	for _, target := range targets.targets {
		target.enqueueResync()
	}
}

func (targets *ReplicationTargets) StatisticsString() string {
	result := ""
	for _, target := range targets.targets {
		result = fmt.Sprintf(
			"%s" +
			"replication_%s_%s_queue_length %d\n",
			result,
			target.hostname, target.port, target.queueLength())
	}
	return result
}
