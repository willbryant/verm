package main

import "net"
import "net/http"
import "time"

type ConnectionTracker struct {
	opened    chan net.Conn
	closed    chan net.Conn
	shutdown  chan *time.Timer
	finished  chan struct{}
	ConnState func(net.Conn, http.ConnState)
}

func NewConnectionTracker() (tracker *ConnectionTracker) {
	tracker = &ConnectionTracker{
		opened:   make(chan net.Conn),
		closed:   make(chan net.Conn),
		shutdown: make(chan *time.Timer),
		finished: make(chan struct{}),
	}
	tracker.ConnState = func(nc net.Conn, state http.ConnState) {
		switch state {
		case http.StateNew:
			tracker.opened <- nc
		case http.StateClosed, http.StateHijacked:
			tracker.closed <- nc
		}
	}
	go tracker.track()
	return
}

func (tracker *ConnectionTracker) Shutdown(timeout time.Duration) {
	tracker.shutdown <- time.NewTimer(timeout)
	<-tracker.finished
}

func (tracker *ConnectionTracker) track() {
	connections := map[net.Conn]struct{}{}
	var deadline <-chan time.Time
	for deadline == nil || len(connections) > 0 {
		select {
		case nc := <-tracker.opened:
			connections[nc] = struct{}{}

		case nc := <-tracker.closed:
			delete(connections, nc)

		case timeout := <-tracker.shutdown:
			deadline = timeout.C

			for nc := range connections {
				nc.(*net.TCPConn).CloseRead()
			}

		case <-deadline:
			for nc := range connections {
				nc.Close()
			}
		}
	}
	close(tracker.finished)
}
