package verm

import "log"
import "mime"
import "net/http"
import "os"
import "path"
import "path/filepath"
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
	atomic.AddUint64(&server.Statistics.get_requests, 1)

	// deal with '/..' etc.
	path := path.Clean(req.URL.Path)

	// try and open the file
	file, stat, err := server.openFile(path)
	if err != nil {
		atomic.AddUint64(&server.Statistics.get_requests_not_found, 1)
		http.NotFound(w, req)
		return
	}
	defer file.Close()

	// infer the content-type from the filename extension
	contenttype := mime.TypeByExtension(filepath.Ext(path))

	// because verm files are immutable, we can use the path as a constant etag for the file
	etag := path

	// if the client supplied cache-checking header, test them
	if checkLastModified(w, req, stat.ModTime()) || checkETag(w, req, etag) {
		// the client is up-to-date
		w.Header().Del("Content-Length")
		w.WriteHeader(http.StatusNotModified)
	} else {
		// send the file
		w.Header().Set("etag", path)
		w.Header().Set("Last-Modified", stat.ModTime().UTC().Format(http.TimeFormat))
		serveContent(w, req, contenttype, stat.Size(), file)
	}
}

func (server vermServer) openFile(path string) (http.File, os.FileInfo, error) {
	file, openerr := server.RootDataDir.Open(path)
	if openerr != nil {
		return nil, nil, openerr
	}

	stat, staterr := file.Stat()
	if staterr != nil || stat.IsDir() {
		file.Close()
		return nil, nil, staterr
	}

	return file, stat, nil
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
