package main

import "bufio"
import "bytes"
import "compress/gzip"
import "fmt"
import "io"
import "os"
import "path"
import "net/http"

func (server vermServer) serveMissing(w http.ResponseWriter, req *http.Request) error {
	w.Header().Set("Content-Type", "text/plain")

	encoding := req.Header.Get("Content-Encoding")
	input, err := EncodingDecoder(encoding, req.Body)
	if err != nil {
		return err
	}

	// we have to buffer the response because the http package kills the request input stream as soon as we start writing to the response stream
	var buf bytes.Buffer

	if gzipAccepted(req) {
		w.Header().Set("Content-Encoding", "gzip")
		w.Header().Set("Content-Type", "text/plain")

		compressor := gzip.NewWriter(&buf)
		server.listMissingFiles(input, compressor)
		compressor.Close()
	} else {
		server.listMissingFiles(input, &buf)
	}

	w.WriteHeader(http.StatusOK)
	_, err = buf.WriteTo(w)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Couldn't write response: %s\n", err.Error())
	}

	return nil
}

func (server vermServer) listMissingFiles(input io.Reader, output io.Writer) {
	scanner := bufio.NewScanner(input)

	for scanner.Scan() {
		line := scanner.Text()

		if (!pathExists(server.RootDataDir, line) &&
			!pathExists(server.RootDataDir, line + ".gz")) {
			_, err := io.WriteString(output, line + "\r\n")
			if err != nil {
				fmt.Fprintf(os.Stderr, "Couldn't write to response buffer: %s\n", err.Error())
			}
		}
	}

	if scanner.Err() != nil {
		fmt.Fprintf(os.Stderr, "Error reading file list: %s\n", scanner.Err().Error())
	}
}

func pathExists(root, filepath string) bool {
	// any errors are treated as missing files, since this causes a replication attempt which will show the real error
	fileinfo, err := os.Stat(root + path.Clean(filepath))
	return (err == nil && fileinfo.Mode().IsRegular())
}
