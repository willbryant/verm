package main

import "io"
import "net/http"
import "os"
import "path"
import "path/filepath"
import "sync/atomic"
import "verm/mimeext"

type vermServer struct {
	RootDataDir string
	RootHttpDir http.Dir
	Targets     *ReplicationTargets
	Statistics  *LogStatistics
	Quiet       bool
}

func VermServer(rootDataDirectory string, mimeTypesFile string, replicationTargets *ReplicationTargets, quiet bool) vermServer {
	mimeext.LoadMimeFile(mimeTypesFile)

	statistics := &LogStatistics{}

	replicationTargets.Start(rootDataDirectory, statistics)
	replicationTargets.EnqueueResync()

	return vermServer{
		RootDataDir: rootDataDirectory,
		RootHttpDir: http.Dir(rootDataDirectory),
		Targets: replicationTargets,
		Statistics: statistics,
		Quiet: quiet,
	}
}

func (server vermServer) serveRoot(w http.ResponseWriter, req *http.Request) {
	w.WriteHeader(http.StatusOK)
	io.WriteString(w,
		"<!DOCTYPE html><html><head><title>Verm - Upload</title></head><body>" +
		"<!-- this form will let you test out verm manually.  don't emulate it in API clients - it's simpler to use raw posts.  you should also insist on posting to an application-specific directory name. -->" +
		"<form method='post' enctype='multipart/form-data'>" +
		"<input type='hidden' name='redirect' value='1'/>" /* redirect instead of returning 201 (as APIs want) */ +
		"<input type='file' name='uploaded_file'/>" +
		"<input type='submit' value='Upload'/>" +
		"</form>" +
		"</body></html>\n")
}

func (server vermServer) serveFile(w http.ResponseWriter, req *http.Request) {
	// deal with '/..' etc.
	path := path.Clean(req.URL.Path)

	// try and open the file
	file, stat, err := server.openFile(path)
	storedCompressed := false
	if err != nil {
		file, stat, err = server.openFile(path + ".gz")
		storedCompressed = true
	}
	if err != nil && server.shouldForwardRead(req) && server.forwardRead(w, req) {
		atomic.AddUint64(&server.Statistics.get_requests, 1)
		atomic.AddUint64(&server.Statistics.get_requests_found_on_replica, 1)
		return
	}
	if err != nil {
		atomic.AddUint64(&server.Statistics.get_requests, 1)
		atomic.AddUint64(&server.Statistics.get_requests_not_found, 1)
		http.NotFound(w, req)
		return
	}
	defer file.Close()
	defer atomic.AddUint64(&server.Statistics.get_requests, 1)

	// if the client supplied cache-checking header, test them
	// because verm files are immutable, we can use the path as a constant etag for the file
	if checkLastModified(w, req, stat.ModTime()) || checkETag(w, req, path) {
		// the client is up-to-date
		w.Header().Del("Content-Length")
		w.WriteHeader(http.StatusNotModified)
	} else {
		w.Header().Set("Last-Modified", stat.ModTime().UTC().Format(http.TimeFormat))
		w.Header().Set("ETag", path)

		// infer the content-type from the filename extension
		contentType := mimeext.TypeByExtension(filepath.Ext(path))
		if contentType == "" {
			// we must set a header to avoid go sniffing the content and setting the header for us, which leads to
			// problems like gzip content-encoded data getting also described as having application/x-gzip content type
			w.Header().Set("Content-Type", "application/octet-stream")
		} else {
			w.Header().Set("Content-Type", contentType)
		}

		// send the file
		if !storedCompressed {
			serveContent(w, req, stat.Size(), file)

		} else if gzipAccepted(req) {
			w.Header().Set("Content-Encoding", "gzip")
			serveContent(w, req, stat.Size(), file)

		} else {
			w.WriteHeader(http.StatusOK)

			if req.Method != "HEAD" {
				unpackAndServeContent(w, file)
			}
		}
	}
}

func (server vermServer) openFile(path string) (http.File, os.FileInfo, error) {
	file, err := server.RootHttpDir.Open(path)
	if err != nil {
		return nil, nil, err
	}

	stat, err := file.Stat()
	if err != nil {
		file.Close()
		return nil, nil, err
	}
	if stat.IsDir() {
		file.Close()
		return nil, nil, &IsDirectoryError{path: path}
	}

	return file, stat, nil
}

type IsDirectoryError struct {
	path string
}

func (e *IsDirectoryError) Error() string {
	return e.path + " is a directory"
}

func (server vermServer) serveHTTPGetOrHead(w http.ResponseWriter, req *http.Request) {
	if req.URL.Path == "/" {
		server.serveRoot(w, req)
	} else if req.URL.Path == "/_statistics" {
		server.serveStatistics(w, req, server.Targets)
	} else {
		server.serveFile(w, req)
	}
}

func (server vermServer) serveHTTPPost(w http.ResponseWriter, req *http.Request) {
	defer atomic.AddUint64(&server.Statistics.post_requests, 1)

	location, newFile, err := server.UploadFile(w, req, false)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	if newFile {
		atomic.AddUint64(&server.Statistics.post_requests_new_file_stored, 1)
	}

	w.Header().Set("Location", location)
	if req.FormValue("redirect") == "1" {
		w.WriteHeader(http.StatusSeeOther)
	} else {
		w.WriteHeader(http.StatusCreated)
	}
}

func (server vermServer) serveHTTPPut(w http.ResponseWriter, req *http.Request) {
	defer atomic.AddUint64(&server.Statistics.put_requests, 1)

	if req.URL.Path == ReplicationMissingFilesPath {
		server.serveMissing(w, req)
		atomic.AddUint64(&server.Statistics.put_requests_missing_file_checks, 1)
		return
	}

	location, newFile, err := server.UploadFile(w, req, true)
	if err != nil {
		atomic.AddUint64(&server.Statistics.put_requests_failed, 1)
		switch err.(type) {
		case *WrongLocationError:
			http.Error(w, err.Error(), 422)
		default:
			http.Error(w, err.Error(), 500)
		}
		return
	}
	if newFile {
		atomic.AddUint64(&server.Statistics.put_requests_new_file_stored, 1)
	}

	w.Header().Set("Location", location)
	w.WriteHeader(http.StatusCreated)
}

func (server vermServer) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	atomic.AddUint64(&server.Statistics.connections_current, 1)
	defer atomic.AddUint64(&server.Statistics.connections_current, ^uint64(0))

	// we need to keep track of the response code and count the bytes so we can log them below
	logger := &responseLogger{w: w, req: req}

	if req.Method == "GET" || req.Method == "HEAD" {
		server.serveHTTPGetOrHead(logger, req)
	} else if req.Method == "POST" {
		server.serveHTTPPost(logger, req)
	} else if req.Method == "PUT" {
		server.serveHTTPPut(logger, req)
	} else {
		http.Error(logger, "Method not supported", 405)
	}

	if !server.Quiet {
		logger.ClfLog()
	}
}
