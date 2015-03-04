package main

import "net"
import "net/http"

type ConnectionTracker struct {
	opened    chan net.Conn
	closed    chan net.Conn
	shutdown  chan chan struct{}
	ConnState func(net.Conn, http.ConnState)
}

func NewConnectionTracker() (tracker *ConnectionTracker) {
	tracker = &ConnectionTracker{
		opened:   make(chan net.Conn),
		closed:   make(chan net.Conn),
		shutdown: make(chan chan struct{}),
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

func (tracker *ConnectionTracker) Shutdown() {
	done := make(chan struct{})
	tracker.shutdown <- done
	<-done
}

func (tracker *ConnectionTracker) track() {
	connections := map[net.Conn]struct{}{}
	var done chan struct{}
	for done == nil || len(connections) > 0 {
		select {
		case nc := <-tracker.opened:
			connections[nc] = struct{}{}

		case nc := <-tracker.closed:
			delete(connections, nc)

		case done = <-tracker.shutdown:
			for nc := range connections {
				nc.Close()
			}
		}
	}
	done <- struct{}{}
}
