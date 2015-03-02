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
				fmt.Fprintf(os.Stderr, "Error scanning %s: %s\n", directory, err.Error())
				return err
			}
		}

		for _, fileinfo := range list {
			if len(fileinfo.Name()) < 7 || fileinfo.Name()[0:7] != "_upload" {
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
}

func (target *ReplicationTarget) sendFileLists(locations <-chan string) {
	for {
		location := <- locations

		if location == "" {
			// channel closed before anything was received
			return
		}

		var buf bytes.Buffer
		compressor := gzip.NewWriter(&buf)

		for location != "" {
			// the request bodies are simply a list of all the locations, one per line.
			io.WriteString(compressor, location)
			io.WriteString(compressor, "\r\n")

			// the compressor flushes output through to the backing buffer periodically.  if this
			// pushes its size up to the target batch size, send a request.  note that we have to
			// use a byte buffer rather than streaming straight to the HTTP request, because when
			// requests fail we have to retry the same list.
			if buf.Len() > ReplicationMissingFilesBatchSize {
				break
			}

			select {
			case location = <- locations:

			case <-time.After(time.Second * ReplicationMissingFilesBatchTime):
				location = ""
			}
		}

		// send the list of locations
		compressor.Close() // Flush isn't enough, we have to close and make a new compressor to get the gzip stream terminated
		target.sendFileListUntilSuccessful(buf.Bytes())
	}
}

func (target *ReplicationTarget) sendFileListUntilSuccessful(data []byte) {
	input := bytes.NewReader(data)
	for attempts := uint(1); ; attempts++ {
		input.Seek(0, 0)
		if target.sendFileList(input) {
			break
		}
		time.Sleep(backoffTime(attempts))
	}
}

func (target *ReplicationTarget) sendFileList(input io.Reader) bool {
	path := fmt.Sprintf("http://%s:%s%s", target.hostname, target.port, ReplicationMissingFilesPath)
	req, err := http.NewRequest("PUT", path, input)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error setting up request to %s: %s\n", path, err.Error())
		return false
	}
	req.Header.Add("Content-Type", "text/plain")
	req.Header.Add("Content-Encoding", "gzip")

	resp, err := target.client.Do(req)
	if resp != nil && resp.Body != nil {
		defer resp.Body.Close()
	}

	if err != nil {
		fmt.Fprintf(os.Stderr, "Error requesting %s: %s\n", path, err.Error())
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
	// copy the response to check that it isn't terminated prematurely.  we'd rather directly use
	// bufio.NewScanner on the resp.Body, but scanner.Scan() will return half-lines if the input
	// is closed early, which can happen if the other end goes away halfway through sending the
	// response.  (it is possible to check scanner.Err() but that will return nil if the error was
	// a plain EOF...)
	buf, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error reading missing file list from %s:%s: %s\n", target.hostname, target.port, err.Error())
	}

	encoding := resp.Header.Get("Content-Encoding")
	input, err := EncodingDecoder(encoding, bytes.NewReader(buf))
	if err != nil {
		fmt.Fprintf(os.Stderr, "Couldn't see missing files on %s:%s: %s\n", target.hostname, target.port, err)
		return
	}

	scanner := bufio.NewScanner(input)

	for scanner.Scan() {
		location := scanner.Text()
		target.enqueueMissingFile(location)
	}

	if scanner.Err() != nil {
		fmt.Fprintf(os.Stderr, "Error reading missing file list from %s:%s: %s\n", target.hostname, target.port, scanner.Err().Error())
	}
}
