package main

import "bufio"
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

	if gzipAccepted(req) {
		w.Header().Set("Content-Encoding", "gzip")
		w.Header().Set("Content-Type", "text/plain")
		compressor := gzip.NewWriter(w)
		defer compressor.Close()
		server.listMissingFiles(input, compressor)
		compressor.Flush()
	} else {
		server.listMissingFiles(input, w)
	}

	return nil
}

func (server vermServer) listMissingFiles(input io.Reader, output io.Writer) {
	scanner := bufio.NewScanner(input)

	for scanner.Scan() {
		line := scanner.Text()

		fmt.Fprintf(os.Stderr, "Checking if '%s' exists\n", line)
		if (!pathExists(server.RootDataDir, line) &&
			!pathExists(server.RootDataDir, line + ".gz")) {
			fmt.Fprintf(os.Stderr, "'%s' needs replication\n", line)
			_, err := io.WriteString(output, line + "\r\n")
			if err != nil {
				fmt.Fprintf(os.Stderr, "Couldn't write to response compressor: %s\n", err.Error())
			}
		}
	}
}

func pathExists(root, filepath string) bool {
	// any errors are treated as missing files, since this causes a replication attempt which will show the real error
	fileinfo, err := os.Stat(root + path.Clean(filepath))
	return (err == nil && fileinfo.Mode().IsRegular())
}
