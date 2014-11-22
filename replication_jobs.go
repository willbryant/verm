package main

import "fmt"
import "io/ioutil"
import "os"
import "net/http"
import "time"

func Put(hostname, port, location, rootDataDirectory string) bool {
	encoding := "gzip"
	input, err := os.Open(rootDataDirectory + location + ".gz")
	if err != nil {
		input, err = os.Open(rootDataDirectory + location)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s\n", err.Error())
			return false
		}
		encoding = ""
	}
	defer input.Close()

	req, err := http.NewRequest("PUT", fmt.Sprintf("http://%s:%s%s", hostname, port, location), input)
	req.Header.Add("Content-Type", "application/octet-stream") // don't need to know the original type, just replicate the filename
	if encoding != "" {
		req.Header.Add("Content-Encoding", encoding)
	}

	client := &http.Client{Timeout: ReplicationHttpTimeout * time.Second}
	resp, err := client.Do(req)
	if resp != nil && resp.Body != nil {
		defer resp.Body.Close()
	}

	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		return false

	} else if resp.StatusCode != 201 {
		body, _ := ioutil.ReadAll(resp.Body)
		fmt.Fprintf(os.Stderr, "Couldn't replicate %s to %s:%s (%d): %s\n", location, hostname, port, resp.StatusCode, body)
		return false

	} else {
		return true
	}
}
