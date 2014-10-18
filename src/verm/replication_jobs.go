package verm

import "fmt"
import "io/ioutil"
import "log"
import "os"
import "net/http"

type ReplicationJob struct {
	location string
	filename string
	content_type string
	encoding string
}

func (job *ReplicationJob) Put(hostname, port string) bool {
	file, err := os.Open(job.filename)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()

	req, err := http.NewRequest("PUT", fmt.Sprintf("http://%s:%s%s", hostname, port, job.location), file)
	req.Header.Add("Content-Type", job.content_type)
	if job.encoding != "" {
		req.Header.Add("Content-Encoding", job.encoding)
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
		log.Printf("Couldn't replicate %s to %s (%d): %s\n", job.location, hostname, resp.StatusCode, body)
		return false

	} else {
		return true
	}
}
