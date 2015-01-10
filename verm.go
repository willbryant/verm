package main

import "flag"
import "fmt"
import "net/http"
import "os"
import "os/signal"
import "strings"
import "syscall"

func main() {
	var rootDataDirectory, listenAddress, port, mimeTypesFile string
	var replicationTargets ReplicationTargets
	var healthCheckPath, healthyIfFile, healthyUnlessFile string
	var quiet bool

	flag.StringVar(&rootDataDirectory, "data", DefaultRoot, "Sets the root data directory to /foo.  Must be fully-qualified (ie. it must start with a /).")
	flag.StringVar(&listenAddress, "listen", DefaultListenAddress, "Listen on the given IP address.  Default: listen on all network interfaces.")
	flag.StringVar(&port, "port", DefaultPort, "Listen on the given port.")
	flag.StringVar(&mimeTypesFile, "mime-types-file", DefaultMimeTypesFile, "Load MIME content-types from the given file.")
	flag.Var(&replicationTargets, "replicate-to", "Replicate files to the given Verm server.  May be given multiple times.")
	flag.BoolVar(&quiet, "quiet", false, "Quiet mode.  Don't print startup/shutdown/request log messages to stdout.")
	flag.StringVar(&healthCheckPath, "health-check-path", "", "Treat requests to this path as health checks from your load balancer, and give a 200 response without trying to serve a file.")
	flag.StringVar(&healthyIfFile, "healthy-if-file", "", "Respond to requests to the health-check-path with a 503 response code if this file doesn't exist.")
	flag.StringVar(&healthyUnlessFile, "healthy-unless-file", "", "Respond to requests to the health-check-path with a 503 response code if this file exists.")
	flag.VisitAll(setFlagFromEnvironment)
	flag.Parse()

	if !quiet {
		fmt.Fprintf(os.Stdout, "Verm listening on http://%s:%s, data in %s\n", listenAddress, port, rootDataDirectory)
	}

	go waitForSignals(&replicationTargets)
	if healthCheckPath != "" {
		http.Handle(AddRoot(healthCheckPath), HealthCheckServer(healthyIfFile, healthyUnlessFile))
	}
	http.Handle("/", VermServer(rootDataDirectory, mimeTypesFile, &replicationTargets, quiet))
	err := http.ListenAndServe(listenAddress + ":" + port, nil)
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
	env := "VERM_" + strings.Replace(strings.ToUpper(f.Name), "-", "_", -1)
	if os.Getenv(env) != "" {
		flag.Set(f.Name, os.Getenv(env))
	}
}