package main

import "math/rand"
import "time"

func backoffTime(failures uint) time.Duration {
	if failures < 2 {
		return ReplicationBackoffBaseDelay*time.Second
	}

	backoffTime := ReplicationBackoffBaseDelay*(2 << (failures - 3))

	if backoffTime > 1 {
		backoffTime = backoffTime/2 + int(rand.Int63n(int64(backoffTime)/2))
	}

	if backoffTime > ReplicationBackoffMaxDelay {
		return ReplicationBackoffMaxDelay*time.Second
	} else {
		return time.Duration(backoffTime)*time.Second
	}
}
