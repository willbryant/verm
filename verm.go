package main

import "flag"
import "fmt"
import "net"
import "net/http"
import "os"
import "os/signal"
import "runtime"
import "runtime/pprof"
import "strings"
import "time"
import "syscall"
import "github.com/willbryant/verm/mimeext"

var compiled_version string

func banner() string {
	if compiled_version != "" {
		return "Verm " + compiled_version
	} else {
		return "Verm"
	}
}

func main() {
	var rootDataDirectory, listenAddress, port, mimeTypesFile string
	var mimeTypesClear bool
	var replicationTargets ReplicationTargets
	var replicationWorkers int
	var healthCheckPath, healthyIfFile, healthyUnlessFile string
	var quiet bool

	flag.StringVar(&rootDataDirectory, "data", DefaultRoot, "Sets the root data directory to /foo.  Must be fully-qualified (ie. it must start with a /).")
	flag.StringVar(&listenAddress, "listen", DefaultListenAddress, "Listen on the given IP address.  Default: listen on all network interfaces.")
	flag.StringVar(&port, "port", DefaultPort, "Listen on the given port.")
	flag.StringVar(&mimeTypesFile, "mime-types-file", DefaultMimeTypesFile, "Load MIME content-types from the given file.")
	flag.BoolVar(&mimeTypesClear, "no-default-mime-types", false, "Clear the built-in MIME types so the settings in the file given in the mime-types-file option are used exclusively.")
	flag.Var(&replicationTargets, "replicate-to", "Replicate files to the given Verm server.  May be given multiple times.")
	flag.IntVar(&replicationWorkers, "replication-workers", runtime.NumCPU()*10, "Number of gophers to use to replicate files to each Verm server.  Generally should be large; the default scales with the number of CPUs detected.")
	flag.BoolVar(&quiet, "quiet", false, "Quiet mode.  Don't print startup/shutdown/request log messages to stdout.")
	flag.StringVar(&healthCheckPath, "health-check-path", "", "Treat requests to this path as health checks from your load balancer, and give a 200 response without trying to serve a file.")
	flag.StringVar(&healthyIfFile, "healthy-if-file", "", "Respond to requests to the health-check-path with a 503 response code if this file doesn't exist.")
	flag.StringVar(&healthyUnlessFile, "healthy-unless-file", "", "Respond to requests to the health-check-path with a 503 response code if this file exists.")
	flag.VisitAll(setFlagFromEnvironment)
	flag.Parse()

	runtime.GOMAXPROCS(runtime.NumCPU())

	mimeext.LoadMimeFile(mimeTypesFile, mimeTypesClear)

	listener, err := net.Listen("tcp", listenAddress + ":" + port)

	if err != nil {
		fmt.Fprintf(os.Stderr, "Couldn't listen on %s:%s: %s\n", listenAddress, port, err.Error())
		os.Exit(1)
	} else if !quiet {
		fmt.Fprintf(os.Stdout, "%s listening on http://%s:%s, data in %s\n", banner(), listenAddress, port, rootDataDirectory)
	}

	statistics := &LogStatistics{}
	server := VermServer(listener, rootDataDirectory, &replicationTargets, statistics, quiet)
	replicationTargets.Start(rootDataDirectory, statistics, replicationWorkers)
	replicationTargets.EnqueueResync()
	go waitForSignals(&server, &replicationTargets)

	if healthCheckPath != "" {
		http.Handle(AddRoot(healthCheckPath), HealthCheckServer(healthyIfFile, healthyUnlessFile))
	}
	http.Handle("/", server)

	server.Serve()

	if err != nil {
		fmt.Fprintf(os.Stderr, "Unexpected error: %s\n", err.Error())
		os.Exit(1)
	}

	server.Tracker.Shutdown(ShutdownResponseTimeout * time.Second)

	os.Exit(0)
}

func waitForSignals(server *vermServer, targets *ReplicationTargets) {
	signals := make(chan os.Signal, 4)
	signal.Notify(signals, syscall.SIGINT)
	signal.Notify(signals, syscall.SIGTERM)
	signal.Notify(signals, syscall.SIGUSR1)
	signal.Notify(signals, syscall.SIGUSR2)
	for {
		switch <-signals {
		case syscall.SIGUSR1:
			fmt.Fprintf(os.Stdout, "Resyncing by request\n")
			targets.EnqueueResync()

		case syscall.SIGUSR2:
			pprof.Lookup("goroutine").WriteTo(os.Stdout, 1)

		case syscall.SIGINT, syscall.SIGTERM:
			if !server.Quiet {
				fmt.Fprintf(os.Stdout, "Verm shutting down by request\n")
			}
			server.Shutdown()
		}
	}
}

func setFlagFromEnvironment(f *flag.Flag) {
	env := "VERM_" + strings.Replace(strings.ToUpper(f.Name), "-", "_", -1)
	if os.Getenv(env) != "" {
		flag.Set(f.Name, os.Getenv(env))
	}
}
