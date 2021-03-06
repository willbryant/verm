package main

import "fmt"
import "io"
import "io/ioutil"
import "os"
import "net/http"

func Put(client *http.Client, hostname, port, location, rootDataDirectory string) bool {
	encoding := "gzip"
	input, err := os.Open(rootDataDirectory + location + ".gz")
	if err != nil {
		input, err = os.Open(rootDataDirectory + location)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Couldn't open file for replication: %s\n", err.Error())
			return false
		}
		encoding = ""
	}
	defer input.Close()

	path := fmt.Sprintf("http://%s:%s%s", hostname, port, location)
	req, err := http.NewRequest("PUT", path, input)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error setting up request for %s: %s\n", path, err.Error())
		return false
	}
	req.Header.Add("Content-Type", "application/octet-stream") // don't need to know the original type, just replicate the filename
	if encoding != "" {
		req.Header.Add("Content-Encoding", encoding)
	}

	resp, err := client.Do(req)
	if resp != nil && resp.Body != nil {
		defer resp.Body.Close()
	}

	if err != nil {
		fmt.Fprintf(os.Stderr, "Error replicating to %s: %s\n", path, err.Error())
		return false

	} else if resp.StatusCode != 201 {
		body, _ := ioutil.ReadAll(resp.Body)
		fmt.Fprintf(os.Stderr, "HTTP error replicating %s: %d %s\n", path, resp.StatusCode, body)
		return false

	} else {
		io.Copy(ioutil.Discard, resp.Body)
		return true
	}
}
