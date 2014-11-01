package verm

import "sync/atomic"
import "time"

type ReplicationTarget struct {
	hostname string
	port string
	jobs chan string
	root_data_directory string
	statistics *LogStatistics
}

func (target *ReplicationTarget) Replicate() {
	var failures uint
	for {
		location := <- target.jobs

		ok := Put(target.hostname, target.port, location, target.root_data_directory)

		if ok {
			atomic.AddUint64(&target.statistics.replication_push_attempts, 1)
			failures = 0
		} else {
			target.jobs <- location
			atomic.AddUint64(&target.statistics.replication_push_attempts, 1)
			atomic.AddUint64(&target.statistics.replication_push_attempts_failed, 1)
			failures++
			time.Sleep(backoffTime(failures))
		}
	}
}

func (target *ReplicationTarget) queueLength() int {
	return len(target.jobs)
}
