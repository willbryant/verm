package main

import "fmt"
import "io"
import "net"
import "net/http"
import "regexp"
import "os"
import "time"

var proxyTransport *http.Transport = &http.Transport{
	// stop roundTrip() interfering with the default verm handling of compression
	DisableCompression: true,

	// remaining stuff as per http.DefaultTransport, though with our timeout:
	Proxy: http.ProxyFromEnvironment,
	Dial: (&net.Dialer{
		Timeout:   ReplicaProxyTimeout * time.Second,
		KeepAlive: ReplicaProxyTimeout * time.Second,
	}).Dial,
	TLSHandshakeTimeout: ReplicaProxyTimeout * time.Second,
}

var proxyClient = &http.Client{
	Timeout:   ReplicaProxyTimeout * time.Second,
	Transport: proxyTransport,
}

var headerFieldsToReturn = []string{
	"Content-Type",
	"Content-Encoding",
	"Content-Length",
	"Content-Range",
	"Last-Modified",
	"ETag",
}

func copyHeaderField(src, dst http.Header, field string) {
	value := src.Get(field)
	if value != "" {
		dst.Set(field, value)
	}
}

func copyHeaderFields(src, dst http.Header, fields []string) {
	for _, field := range fields {
		copyHeaderField(src, dst, field)
	}
}

var hashlikeExpression = regexp.MustCompile("/[A-Za-g][A-Za-z0-9_-]/[A-Za-g][A-Za-z0-9_-]{40}(\\.[A-Za-z0-9]+|$)")

func (server vermServer) shouldForwardRead(req *http.Request) bool {
	param := req.URL.Query()["forward"]
	if len(param) != 0 && param[len(param) - 1] == "0" {
		return false
	}
	return hashlikeExpression.MatchString(req.URL.Path)
}

func (server vermServer) forwardRead(w http.ResponseWriter, req *http.Request) bool {
	// we use a separate goroutine to grab the first successful response because we want it to hang
	// around and close any slower connections so they can be reused (per http docs)
	ch := make(chan *http.Response)
	go server.Targets.forwardRequest(w, req, ch)
	resp := <-ch

	if resp == nil {
		// all targets failed or didn't have the file
		return false
	}

	// we got a successful response from at least one of the replicas, copy the winning response over
	copyHeaderFields(resp.Header, w.Header(), headerFieldsToReturn)
	w.WriteHeader(http.StatusOK)
	io.Copy(w, resp.Body)
	resp.Body.Close()
	return true
}

func (targets *ReplicationTargets) forwardRequest(w http.ResponseWriter, req *http.Request, out chan *http.Response) {
	responses := make(chan *http.Response)

	for _, target := range targets.targets {
		go target.forwardRequest(w, req, responses)
	}

	success := false
	for _, _ = range targets.targets {
		resp := <-responses

		// resp will be nil if this target failed
		if resp != nil {
			if success {
				// if we've already passed on another response, just clean up the connection so it can be reused, per http package docs
				resp.Body.Close()
			} else {
				// this response won the race, pass it on
				success = true
				out <- resp
				// keep iterating so we can close any other response bodies as above
			}
		}
	}
	if !success {
		out <- nil
	}

	close(out)
	close(responses)
}

func (target *ReplicationTarget) forwardRequest(w http.ResponseWriter, reqIn *http.Request, out chan *http.Response) {
	reqOut, err := http.NewRequest("GET", fmt.Sprintf("http://%s:%s%s?forward=0", target.hostname, target.port, reqIn.URL.Path), nil)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		out <- nil
	}

	copyHeaderField(reqIn.Header, reqOut.Header, "Accept-Encoding")

	resp, err := proxyClient.Do(reqOut)

	if err != nil {
		// unexpected failure
		fmt.Fprintf(os.Stderr, "Error requesting %s from %s:%s: %s\n", reqIn.URL.Path, target.hostname, target.port, err.Error())
		out <- nil

	} else if resp.StatusCode == http.StatusOK {
		// success - pass on the response and let the other end close the response
		out <- resp

	} else if resp.StatusCode == http.StatusNotFound {
		// normal missing case
		resp.Body.Close()
		out <- nil

	} else {
		// unexpected HTTP error
		fmt.Fprintf(os.Stderr, "HTTP error requesting %s from %s:%s: %s\n", reqIn.URL.Path, target.hostname, target.port, resp.StatusCode)
		resp.Body.Close()
		out <- nil
	}
}
