package verm

import "sync/atomic"
import "time"

type ReplicationTarget struct {
	hostname string
	port string
	jobs chan ReplicationJob
}

func (target *ReplicationTarget) Replicate(statistics *LogStatistics) {
	var failures uint
	for {
		job := <- target.jobs

		ok := job.Put(target.hostname, target.port)

		if ok {
			atomic.AddUint64(&statistics.replication_push_attempts, 1)
			failures = 0
		} else {
			target.jobs <- job
			atomic.AddUint64(&statistics.replication_push_attempts, 1)
			atomic.AddUint64(&statistics.replication_push_attempts_failed, 1)
			failures++
			time.Sleep(backoffTime(failures))
		}
	}
}

func (target *ReplicationTarget) queueLength() int {
	return len(target.jobs)
}
