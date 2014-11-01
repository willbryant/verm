package main

import "flag"
import "fmt"
import "log"
import "net/http"
import "os"
import "os/signal"
import "syscall"
import "verm"

func main() {
	var root_data_directory, listen_address, port, mime_types_file string
	var replication_targets verm.ReplicationTargets
	var quiet bool

	flag.StringVar(&root_data_directory, "d", verm.DEFAULT_ROOT, "Sets the root data directory to /foo.  Must be fully-qualified (ie. it must start with a /).")
	flag.StringVar(&listen_address, "l", verm.DEFAULT_LISTEN_ADDRESS, "Listen on the given IP address.  Default: listen on all network interfaces.")
	flag.StringVar(&port, "p", verm.DEFAULT_VERM_PORT, "Listen on the given port.")
	flag.StringVar(&mime_types_file, "m", verm.DEFAULT_MIME_TYPES_FILE, "Load MIME content-types from the given file.")
	flag.Var(&replication_targets, "r", "Replicate files to the given Verm server.  May be given multiple times.")
	flag.BoolVar(&quiet, "q", false, "Quiet mode.  Don't print startup/shutdown/request log messages to stdout.")
	flag.Parse()

	if !quiet {
		fmt.Printf("Verm listening on http://%s:%s, data in %s\n", listen_address, port, root_data_directory)
	}

	go waitForSignals(&replication_targets)
	http.Handle("/", verm.VermServer(root_data_directory, mime_types_file, &replication_targets, quiet))
	log.Fatal(http.ListenAndServe(listen_address + ":" + port, nil))

	os.Exit(0)
}

func waitForSignals(targets *verm.ReplicationTargets) {
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGUSR1)
	for {
		<- signals
		targets.EnqueueResync()
	}
}
