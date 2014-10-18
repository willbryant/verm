package verm

import "io"
import "log"
import "mimeext"
import "net/http"
import "os"
import "path"
import "path/filepath"
import "sync/atomic"

type vermServer struct {
	RootDataDir string
	RootHttpDir http.Dir
	Statistics *LogStatistics
	Quiet bool
}

func VermServer(root string, mime_types_file string, quiet bool) vermServer {
	mimeext.LoadMimeFile(mime_types_file)

	return vermServer{
		RootDataDir: root,
		RootHttpDir: http.Dir(root),
		Statistics: &LogStatistics{},
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
	stored_compressed := false
	if err != nil {
		file, stat, err = server.openFile(path + ".gz")
		stored_compressed = true
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
		contenttype := mimeext.TypeByExtension(filepath.Ext(path))
		if contenttype != "" {
			w.Header().Set("Content-Type", contenttype)
		}

		// send the file
		if !stored_compressed {
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
	file, openerr := server.RootHttpDir.Open(path)
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

func (server vermServer) serveHTTPGetOrHead(w http.ResponseWriter, req *http.Request) {
	if req.URL.Path == "/" {
		server.serveRoot(w, req)
	} else if req.URL.Path == "/_statistics" {
		server.serveStatistics(w, req)
	} else {
		server.serveFile(w, req)
	}
}

func (server vermServer) serveHTTPPost(w http.ResponseWriter, req *http.Request) {
	defer atomic.AddUint64(&server.Statistics.post_requests, 1)

	location, new_file, err := server.UploadFile(w, req, false)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	if new_file {
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

	location, new_file, err := server.UploadFile(w, req, true)
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
	if new_file {
		atomic.AddUint64(&server.Statistics.put_requests_new_file_stored, 1)
	}

	w.Header().Set("Location", location)
	w.WriteHeader(http.StatusCreated)
}

func (server vermServer) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	atomic.AddUint64(&server.Statistics.connections_current, 1)
	defer atomic.AddUint64(&server.Statistics.connections_current, ^uint64(0))

	if req.Method == "GET" || req.Method == "HEAD" {
		server.serveHTTPGetOrHead(w, req)
	} else if req.Method == "POST" {
		server.serveHTTPPost(w, req)
	} else if req.Method == "PUT" {
		server.serveHTTPPut(w, req)
	} else {
		http.Error(w, "Method not supported", 405)
	}

	if !server.Quiet {
		// TODO: implement CLF-style logging
		log.Printf("%s %s", req.Method, req.URL.Path)
	}
}
