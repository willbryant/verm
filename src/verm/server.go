package verm

import "log"
import "net/http"
import "sync/atomic"

type vermServer struct {
	RootDataDir http.Dir
	Statistics *LogStatistics
	Quiet bool
}

func VermServer(root string, mime_types_file string, quiet bool) vermServer {
	loadMimeFile(mime_types_file)
	return vermServer{RootDataDir: http.Dir(root), Statistics: &LogStatistics{}, Quiet: quiet}
}

func (server vermServer) ServeRoot(w http.ResponseWriter, req *http.Request) {
	// TODO: implement upload form
	http.Error(w, "Nothing to see here yet", 404)
}

func (server vermServer) ServeFile(w http.ResponseWriter, req *http.Request) {
	var file_server = http.FileServer(server.RootDataDir)
	file_server.ServeHTTP(w, req)

	atomic.AddUint64(&server.Statistics.get_requests, 1)
	// TODO: implement get_requests_not_found
}

func (server vermServer) ServeHTTPGetOrHead(w http.ResponseWriter, req *http.Request) {
	if req.URL.Path == "/" {
		server.ServeRoot(w, req)
	} else if req.URL.Path == "/_statistics" {
		server.ServeStatistics(w, req)
	} else {
		server.ServeFile(w, req)
	}
}

func (server vermServer) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	atomic.AddUint64(&server.Statistics.connections_current, 1)
	defer atomic.AddUint64(&server.Statistics.connections_current, ^uint64(0))

	if req.Method == "GET" || req.Method == "HEAD" {
		server.ServeHTTPGetOrHead(w, req)
	} else {
		http.Error(w, "Method not supported", 500)
	}

	if !server.Quiet {
		// TODO: implement CLF-style logging
		log.Printf("%s %s", req.Method, req.URL.Path)
	}
}
