package main

import "time"

func backoffTime(failures uint) time.Duration {
	if failures < 2 {
		return ReplicationBackoffBaseDelay*time.Second
	}

	backoffTime := ReplicationBackoffBaseDelay*(2 << (failures - 3))

	if backoffTime > ReplicationBackoffMaxDelay {
		return ReplicationBackoffMaxDelay*time.Second
	} else {
		return time.Duration(backoffTime)*time.Second
	}
}
