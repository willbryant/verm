package main

import "io"
import "net/http"
import "os"

type healthCheckServer struct {
	HealthyIfFile     string
	HealthyUnlessFile string
}

func HealthCheckServer(healthyIfFile string, healthyUnlessFile string) healthCheckServer {
	return healthCheckServer{
		HealthyIfFile:     AddRoot(healthyIfFile),
		HealthyUnlessFile: AddRoot(healthyUnlessFile),
	}
}

func AddRoot(path string) string {
	if len(path) > 0 && path[0] != '/' {
		return "/" + path
	} else {
		return path
	}
}

func (server healthCheckServer) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	if server.HealthyIfFile != "" {
		_, err := os.Stat(server.HealthyIfFile)
		if os.IsNotExist(err) {
			http.Error(w, "Offline", 503)
			return
		}
	}

	if server.HealthyUnlessFile != "" {
		_, err := os.Stat(server.HealthyUnlessFile)
		if !os.IsNotExist(err) {
			http.Error(w, "Offline", 503)
			return
		}
	}

	w.WriteHeader(http.StatusOK)
	io.WriteString(w, "Online\n")
}
