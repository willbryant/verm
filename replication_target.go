package main

import "net"
import "net/http"
import "sync/atomic"
import "time"

type ReplicationTarget struct {
	hostname          string
	port              string
	newFiles          chan string
	resyncFiles       chan string
	needToResync      chan struct{}
	rootDataDirectory string
	statistics        *LogStatistics
	unfinishedJobs    uint64
	client            *http.Client
}

func NewReplicationTarget(hostname, port string) ReplicationTarget {
	return ReplicationTarget{
		hostname:     hostname,
		port:         port,
	}
}

func (target *ReplicationTarget) Start(rootDataDirectory string, statistics *LogStatistics, workers int) {
	transport := &http.Transport{
		// increase MaxIdleConnsPerHost:
		MaxIdleConnsPerHost: workers + 2,

		// otherwise defaults (as per DefaultTransport):
		Proxy: http.ProxyFromEnvironment,
		Dial: (&net.Dialer{
			Timeout:   ReplicationHttpTimeout * time.Second,
			KeepAlive: ReplicationHttpTimeout * time.Second,
		}).Dial,
		TLSHandshakeTimeout: 10 * time.Second,
	}
	
	target.client = &http.Client{
		Timeout: ReplicationHttpTimeout * time.Second,
		Transport: transport,
	}
	target.rootDataDirectory = rootDataDirectory
	target.statistics = statistics
	target.newFiles =     make(chan string, ReplicationQueueSize - ReplicationResyncQueueSize - workers)
	target.resyncFiles =  make(chan string, ReplicationResyncQueueSize)
	target.needToResync = make(chan struct{}, 1)

	go target.resyncFromQueue()
	for worker := 1; worker < workers; worker++ {
		go target.replicateFromQueue()
	}
}

func (target *ReplicationTarget) enqueueNewFile(location string) {
	select {
	case target.newFiles <- location:
		// successfully queued
		atomic.AddUint64(&target.unfinishedJobs, 1)

	default:
		// queue is full, flag for a resync later so the file eventually gets sent
		target.enqueueResync();
	}
}

func (target *ReplicationTarget) enqueueResyncFile(location string) {
	// it would be silly to resync if the queue is full in this case, so we just wait
	target.resyncFiles <- location
	atomic.AddUint64(&target.unfinishedJobs, 1)
}

func (target *ReplicationTarget) replicateFromQueue() {
	for {
		var location string
		select {
		case location = <- target.newFiles:
		case location = <- target.resyncFiles:
		}
		target.replicate(location)
		atomic.AddUint64(&target.unfinishedJobs, ^uint64(0))
	}
}

func (target *ReplicationTarget) replicate(location string) {
	for attempts := uint(1); ; attempts++ {
		ok := Put(target.client, target.hostname, target.port, location, target.rootDataDirectory)

		if ok {
			atomic.AddUint64(&target.statistics.replication_push_attempts, 1)
			break
		} else {
			atomic.AddUint64(&target.statistics.replication_push_attempts, 1)
			atomic.AddUint64(&target.statistics.replication_push_attempts_failed, 1)
			time.Sleep(backoffTime(attempts))
		}
	}
}

func (target *ReplicationTarget) queueLength() int {
	// we used to simply use len() on the channels, but that makes jobs disappear off the count and
	// reappear later if they fail
	return int(atomic.LoadUint64(&target.unfinishedJobs))
}

func (target *ReplicationTarget) enqueueResync() {
	// the resync "queue" is really a single-entry flag channel; resyncs are idempotent, so if
	// there's already one queued, we don't need to block to queue another.  note that the one
	// in the queue will cause a full resync after completion of resync tasks already running,
	// so files don't get lost in between.
	select {
	case target.needToResync <- struct{}{}:
		// resync queued

	default:
		// resync already queued
	}
}

func (target *ReplicationTarget) resyncFromQueue() {
	for {
		<-target.needToResync

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
