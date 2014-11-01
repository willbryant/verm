package verm

import "fmt"
import "io/ioutil"
import "log"
import "os"
import "net/http"

type ReplicationJob struct {
	location string
	filename string
}

func (job *ReplicationJob) Put(hostname, port string) bool {
	encoding := "gzip"
	input, err := os.Open(job.filename + ".gz")
	if err != nil {
		input, err = os.Open(job.filename)
		if err != nil {
			log.Fatal(err)
		}
		encoding = ""
	}
	defer input.Close()

	req, err := http.NewRequest("PUT", fmt.Sprintf("http://%s:%s%s", hostname, port, job.location), input)
	req.Header.Add("Content-Type", "application/octet-stream") // don't need to know the original type, just replicate the filename
	if encoding != "" {
		req.Header.Add("Content-Encoding", encoding)
	}

	client := &http.Client{}
	resp, err := client.Do(req)
	if resp != nil && resp.Body != nil {
		defer resp.Body.Close()
	}

	if err != nil {
		log.Printf(err.Error())
		return false

	} else if resp.StatusCode != 201 {
		body, _ := ioutil.ReadAll(resp.Body)
		log.Printf("Couldn't replicate %s to %s:%s (%d): %s\n", job.location, hostname, port, resp.StatusCode, body)
		return false

	} else {
		return true
	}
}
