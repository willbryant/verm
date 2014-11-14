package main

import "flag"
import "fmt"
import "net/http"
import "os"
import "os/signal"
import "strings"
import "syscall"

func main() {
	var root_data_directory, listen_address, port, mime_types_file string
	var replication_targets ReplicationTargets
	var quiet bool

	flag.StringVar(&root_data_directory, "data", DEFAULT_ROOT, "Sets the root data directory to /foo.  Must be fully-qualified (ie. it must start with a /).")
	flag.StringVar(&listen_address, "listen", DEFAULT_LISTEN_ADDRESS, "Listen on the given IP address.  Default: listen on all network interfaces.")
	flag.StringVar(&port, "port", DEFAULT_PORT, "Listen on the given port.")
	flag.StringVar(&mime_types_file, "mimetypes", DEFAULT_MIME_TYPES_FILE, "Load MIME content-types from the given file.")
	flag.Var(&replication_targets, "replicate", "Replicate files to the given Verm server.  May be given multiple times.")
	flag.BoolVar(&quiet, "quiet", false, "Quiet mode.  Don't print startup/shutdown/request log messages to stdout.")
	flag.VisitAll(setFlagFromEnvironment)
	flag.Parse()

	if !quiet {
		fmt.Fprintf(os.Stdout, "Verm listening on http://%s:%s, data in %s\n", listen_address, port, root_data_directory)
	}

	go waitForSignals(&replication_targets)
	http.Handle("/", VermServer(root_data_directory, mime_types_file, &replication_targets, quiet))
	err := http.ListenAndServe(listen_address + ":" + port, nil)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err.Error())
		os.Exit(1)
	}

	os.Exit(0)
}

func waitForSignals(targets *ReplicationTargets) {
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGUSR1)
	for {
		<-signals
		targets.EnqueueResync()
	}
}

func setFlagFromEnvironment(f *flag.Flag) {
	env := "VERM_" + strings.ToUpper(f.Name)
	if os.Getenv(env) != "" {
		flag.Set(f.Name, os.Getenv(env))
	}
}