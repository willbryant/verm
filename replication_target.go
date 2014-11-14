package main

import "sync/atomic"
import "time"

type ReplicationTarget struct {
	hostname            string
	port                string
	jobs                chan string
	resync              chan struct{}
	root_data_directory string
	statistics          *LogStatistics
}

func (target *ReplicationTarget) enqueueJob(job string) {
	target.jobs <- job
}

func (target *ReplicationTarget) replicateFromQueue() {
	var failures uint
	for {
		location := <-target.jobs

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

func (target *ReplicationTarget) enqueueResync() {
	// the resync "queue" is really a single-entry flag channel; resyncs are idempotent, so if
	// there's already one queued, we don't need to block to queue another.  note that the one
	// in the queue will cause a full resync after completion of resync tasks already running,
	// so files don't get lost in between.
	select {
	case target.resync <- struct{}{}:
	}
}

func (target *ReplicationTarget) resyncFromQueue() {
	for {
		<-target.resync

		// our thread scans the directory and pushes the filenames found to a channel which is
		// listened to by a second routine, which posts batches of those filenames over to the
		// target and gets back lists of missing files - which it then pushes onto the regular
		// replication job queue.  this provides overall flow control; if the replication jobs
		// don't make it through, there's no point finding more and more files not replicated.
		locations := make(chan string, 1000) // arbitrary buffer to give some concurrency
		go target.sendFileLists(locations)
		target.enumerateFiles(locations)
	}
}
