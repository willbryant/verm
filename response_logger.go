package main

import "fmt"
import "net/http"
import "os"
import "strings"
import "time"

func ClfFormat(t time.Time) string {
	return t.Format("[02/Jan/2006:15:04:05 -0700]")
}

func ClfTime() string {
	return ClfFormat(time.Now())
}

func RequestIP(req *http.Request) string {
	addressEnd := strings.LastIndex(req.RemoteAddr, "]:")
	if addressEnd > -1 {
		return req.RemoteAddr[1:addressEnd]
	}
	return req.RemoteAddr
}

type responseLogger struct {
	req        *http.Request
	w          http.ResponseWriter
	StatusCode int
	Bytes      int
}

func (logger *responseLogger) Header() http.Header {
	return logger.w.Header()
}

func (logger *responseLogger) WriteHeader(status int) {
	logger.StatusCode = status
	logger.w.WriteHeader(status)
}

func (logger *responseLogger) Write(bytes []byte) (int, error) {
	logger.Bytes += len(bytes)
	return logger.w.Write(bytes)
}

func (logger *responseLogger) ClfLog() {
	fmt.Fprintf(os.Stdout, "%s - - %s \"%s %s %s\" %d %d\n",
		RequestIP(logger.req),
		ClfTime(),
		logger.req.Method,
		logger.req.URL.Path,
		logger.req.Proto,
		logger.StatusCode,
		logger.Bytes)
}
