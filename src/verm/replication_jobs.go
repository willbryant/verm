package verm

import "fmt"
import "io/ioutil"
import "log"
import "os"
import "net/http"

func Put(hostname, port, location, root_data_directory string) bool {
	encoding := "gzip"
	input, err := os.Open(root_data_directory + location + ".gz")
	if err != nil {
		input, err = os.Open(root_data_directory + location)
		if err != nil {
			log.Fatal(err)
		}
		encoding = ""
	}
	defer input.Close()

	req, err := http.NewRequest("PUT", fmt.Sprintf("http://%s:%s%s", hostname, port, location), input)
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
		log.Printf("Couldn't replicate %s to %s:%s (%d): %s\n", location, hostname, port, resp.StatusCode, body)
		return false

	} else {
		return true
	}
}
