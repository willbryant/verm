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

func backoffTime(failures uint) time.Duration {
	if failures < 2 {
		return BACKOFF_BASE_TIME*time.Second
	}

	backoff_time := BACKOFF_BASE_TIME*(2 << (failures - 3))

	if backoff_time > BACKOFF_MAX_TIME {
		return BACKOFF_MAX_TIME*time.Second
	} else {
		return time.Duration(backoff_time)*time.Second
	}
}

func (target *ReplicationTarget) queueLength() int {
	return len(target.jobs)
}
