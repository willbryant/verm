package main

import "compress/gzip"
import "errors"
import "io"
import "net/http"
import "regexp"

var gzipExpression = regexp.MustCompile("\\b(x-)?gzip\\b")

func gzipAccepted(req *http.Request) bool {
	accept := req.Header.Get("Accept-Encoding")

	// spec says we should assume any of the "common" encodings are supported - ie. gzip and compress - if not explicitly told
	return accept == "" || gzipExpression.MatchString(accept)
}

type gzipSeeker struct {
	compressed   io.ReadSeeker
	uncompressed *gzip.Reader
	position     int64
	size         int64
}

func NewGzipSeeker(compressed io.ReadSeeker, uncompressed *gzip.Reader) (seeker gzipSeeker, err error) {
	seeker.compressed = compressed
	seeker.uncompressed = uncompressed

	// scan through the entire file so we can determine the length, which is required by serveContent (in
	// order to populate the Content-Range header), and also used to support seeking relative to the end.
	buf := make([]byte, 4096)
	var n int
	for err == nil {
		n, err = uncompressed.Read(buf)
		seeker.size += int64(n)
	}
	if err != io.EOF {
		return
	}

	_, err = compressed.Seek(0, io.SeekStart)
	if err == nil {
		err = uncompressed.Reset(compressed)
	}
	return
}

func (seeker *gzipSeeker) Read(p []byte) (n int, err error) {
	n, err = seeker.uncompressed.Read(p)
	seeker.position += int64(n)
	return
}

func (seeker *gzipSeeker) Seek(offset int64, whence int) (int64, error) {
	switch whence {
	case io.SeekStart:
		if offset < seeker.position {
			seeker.position = 0
			_, err := seeker.compressed.Seek(0, io.SeekStart)
			if err == nil {
				err = seeker.uncompressed.Reset(seeker.compressed)
			}
			if err != nil {
				return 0, err
			}
		}
		buf := make([]byte, 4096)
		for offset > seeker.position {
			bytes := offset - seeker.position
			if bytes > 4096 {
				bytes = 4096
			}
			_, err := seeker.Read(buf[:bytes])
			if err != nil {
				return seeker.position, err
			}
		}
		return seeker.position, nil

	case io.SeekCurrent:
		return seeker.Seek(seeker.position + offset, io.SeekStart)

	case io.SeekEnd:
		return seeker.Seek(seeker.size + offset, io.SeekStart)

	default:
		return seeker.position, errors.New("invalid whence value")
	}
}

func (seeker *gzipSeeker) Size() int64 {
	return seeker.size
}

func unpackAndServeContent(w http.ResponseWriter, req *http.Request, compressed io.ReadSeeker) {
	uncompressed, err := gzip.NewReader(compressed)
	if err != nil {
		http.Error(w, "Couldn't create decompressor", 500)
		return
	}
	defer uncompressed.Close()

	// calculating the size of the uncompressed data is expensive, so we only do it if it's actually required;
	// if we weren't asked for to send only specific byte ranges of the file, we can simply stream the entire
	// file to the client using chunked transfer encoding without finding the size first
	if req.Header.Get("Range") == "" {
		w.WriteHeader(http.StatusOK)
		io.Copy(w, uncompressed)
		return
	}

	seeker, err := NewGzipSeeker(compressed, uncompressed)
	if err != nil {
		http.Error(w, "Couldn't decompress file " + err.Error(), 500)
		return
	}

	serveContent(w, req, seeker.Size(), &seeker)
}
