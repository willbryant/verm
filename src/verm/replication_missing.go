package verm

import "bufio"
import "compress/gzip"
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
		server.listMissingFiles(input, compressor)
		compressor.Close()
	} else {
		server.listMissingFiles(input, w)
	}

	return nil
}

func (server vermServer) listMissingFiles(input io.Reader, output io.Writer) {
	scanner := bufio.NewScanner(input)

	for scanner.Scan() {
		line := scanner.Text()

		if (!pathExists(server.RootDataDir, line) &&
			!pathExists(server.RootDataDir, line + ".gz")) {
			io.WriteString(output, line + "\r\n")
		}
	}
}

func pathExists(root, filepath string) bool {
	// any errors are treated as missing files, since this causes a replication attempt which will show the real error
	fileinfo, err := os.Stat(root + path.Clean(filepath))
	return (err == nil && fileinfo.Mode().IsRegular())
}