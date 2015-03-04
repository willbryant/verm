package main

import "net"
import "net/http"
import "time"

// http/server.go doesn't publish tcpKeepAliveListener, so we have to include it here
// we have done all of this so that we can call Listen ourselves so we can close it!

// tcpKeepAliveListener sets TCP keep-alive timeouts on accepted
// connections. It's used by ListenAndServe and ListenAndServeTLS so
// dead TCP connections (e.g. closing laptop mid-download) eventually
// go away.
type tcpKeepAliveListener struct {
	*net.TCPListener
}

func (ln tcpKeepAliveListener) Accept() (c net.Conn, err error) {
	tc, err := ln.AcceptTCP()
	if err != nil {
		return
	}
	tc.SetKeepAlive(true)
	tc.SetKeepAlivePeriod(3 * time.Minute)
	return tc, nil
}

func (server vermServer) Serve() (error) {
	srv := &http.Server{}
	return srv.Serve(tcpKeepAliveListener{server.Listener.(*net.TCPListener)})
}

func (server vermServer) Close() {
	server.Listener.Close()
}
