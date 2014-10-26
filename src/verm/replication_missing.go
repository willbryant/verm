package verm

import "bufio"
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

	scanner := bufio.NewScanner(input)
	for scanner.Scan() {
		line := scanner.Text()

		if (!pathExists(server.RootDataDir, line) &&
			!pathExists(server.RootDataDir, line + ".gz")) {
			io.WriteString(w, line + "\r\n")
		}
	}

	return nil
}

func pathExists(root, filepath string) bool {
	fileinfo, err := os.Stat(root + path.Clean(filepath))
	return (err == nil && fileinfo.Mode().IsRegular())
}