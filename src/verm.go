package main

import "flag"
import "fmt"
import "log"
import "net/http"
import "os"

const default_root = "/var/lib/verm"
const default_directory_if_not_given_by_client = "/default" // rather than letting people upload directly into the root directory, which in practice is a PITA to administer.  no command-line option for this because it should be provided by the client, so letting admins change it implies mis-use by the client which would be a problem down the track.
const default_listen_address = "0.0.0.0"
const default_http_port = "1138"
const default_mime_types_file = "/etc/mime.types"

func main() {
	var root_data_directory, listen_address, port, mime_types_file string
	var quiet bool

	flag.StringVar(&root_data_directory, "d", default_root, "Sets the root data directory to /foo.  Must be fully-qualified (ie. it must start with a /).")
	flag.StringVar(&listen_address, "l", default_listen_address, "Listen on the given IP address.  Default: listen on all network interfaces.")
	flag.StringVar(&port, "p", default_http_port, "Listen on the given port.")
	flag.StringVar(&mime_types_file, "m", default_mime_types_file, "Load MIME content-types from the given file.")
	flag.BoolVar(&quiet, "q", false, "Quiet mode.  Don't print startup/shutdown/request log messages to stdout.")
	flag.Parse()

	if !quiet {
		fmt.Printf("Verm listening on http://%s:%s, writing to %s, mime types in %s\n", listen_address, port, root_data_directory, mime_types_file)
	}

	log.Fatal(http.ListenAndServe(listen_address + ":" + port, nil))

	os.Exit(0)
}
