package main

import "bufio"
import "bytes"
import "compress/gzip"
import "fmt"
import "io"
import "io/ioutil"
import "os"
import "net/http"
import "strings"
import "time"

func (target *ReplicationTarget) enumerateFiles(locations chan<- string) {
	target.enumerateSubdirectory("", locations)
	close(locations)
}

func (target *ReplicationTarget) enumerateSubdirectory(directory string, locations chan<- string) error {
	dir, err := os.Open(target.rootDataDirectory + directory)
	if err != nil {
		return err
	}
	defer dir.Close()

	for {
		list, err := dir.Readdir(1000)

		if len(list) == 0 {
			if err == io.EOF {
				return nil
			} else if err != nil {
				return err
			}
		}

		for _, fileinfo := range list {
			expanded := fmt.Sprintf("%s%c%s", directory, os.PathSeparator, fileinfo.Name())
			if fileinfo.Mode().IsRegular() {
				locations <- strings.TrimSuffix(expanded, ".gz")
			} else if fileinfo.Mode().IsDir() {
				target.enumerateSubdirectory(expanded, locations)
			} else {
				fmt.Fprintf(os.Stderr, "Ignoring irregular directory entry %s\n", expanded)
			}
		}
	}
}

func (target *ReplicationTarget) sendFileLists(locations <-chan string) {
	var buf bytes.Buffer
	compressor := gzip.NewWriter(&buf)
	somethingToSend := false

	for location := range locations {
		somethingToSend = true

		// the request bodies are simply a list of all the locations, one per line.
		io.WriteString(compressor, location)
		io.WriteString(compressor, "\r\n")

		// the compressor flushes output through to the backing buffer periodically.  if this
		// pushes its size up to the target batch size, send a request.  note that we have to
		// use a byte buffer rather than streaming straight to the HTTP request, because when
		// requests fail we have to retry the same list.
		if buf.Len() > ReplicationMissingFilesBatchSize {
			target.sendFileListUntilSuccessful(compressor, &buf)
			somethingToSend = false
		}
	}
	if somethingToSend {
		target.sendFileListUntilSuccessful(compressor, &buf)
	}
}

func (target *ReplicationTarget) sendFileListUntilSuccessful(compressor *gzip.Writer, buf *bytes.Buffer) {
	compressor.Flush()
	input := bytes.NewReader(buf.Bytes())
	for attempts := 1; ; attempts++ {
		input.Seek(0, 0)
		if target.sendFileList(input) {
			break
		}
		time.Sleep(backoffTime(uint(attempts)))
	}
	buf.Reset()
	compressor.Reset(buf)
}

func (target *ReplicationTarget) sendFileList(input io.Reader) bool {
	req, err := http.NewRequest("PUT", fmt.Sprintf("http://%s:%s%s", target.hostname, target.port, ReplicationMissingFilesPath), input)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		return false
	}
	req.Header.Add("Content-Type", "text/plain")
	req.Header.Add("Content-Encoding", "gzip")

	client := &http.Client{}
	resp, err := client.Do(req)
	if resp != nil && resp.Body != nil {
		defer resp.Body.Close()
	}

	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		return false

	} else if resp.StatusCode != 200 {
		body, _ := ioutil.ReadAll(resp.Body)
		fmt.Fprintf(os.Stderr, "Couldn't see missing files on %s:%s (%d): %s\n", target.hostname, target.port, resp.StatusCode, body)
		return false
	}

	target.queueMissingFiles(resp)
	return true
}

func (target *ReplicationTarget) queueMissingFiles(resp *http.Response) {
	encoding := resp.Header.Get("Content-Encoding")
	input, err := EncodingDecoder(encoding, resp.Body)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Couldn't see missing files on %s:%s: %s\n", target.hostname, target.port, err)
		return
	}

	scanner := bufio.NewScanner(input)

	for scanner.Scan() {
		location := scanner.Text()
		target.enqueueJob(location)
	}
}
