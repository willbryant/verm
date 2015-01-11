package main

import "compress/gzip"
import "io"
import "net/http"
import "regexp"

var gzipExpression = regexp.MustCompile("\\b(x-)?gzip\\b")

func gzipAccepted(req *http.Request) bool {
	accept := req.Header.Get("Accept-Encoding")

	// spec says we should assume any of the "common" encodings are supported - ie. gzip and compress - if not explicitly told
	return accept == "" || gzipExpression.MatchString(accept)
}

func unpackAndServeContent(w http.ResponseWriter, compressed io.Reader) {
	uncompressed, err := gzip.NewReader(compressed)
	if err != nil {
		http.Error(w, "Couldn't create decompressor", 500)
		return
	}
	defer uncompressed.Close()
	io.Copy(w, uncompressed)
}
