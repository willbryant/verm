package main

func backoffTime(failures uint) int {
	if failures < 2 {
		return ReplicationBackoffBaseDelay
	}

	backoffTime := ReplicationBackoffBaseDelay*(2 << (failures - 2))

	if backoffTime > ReplicationBackoffMaxDelay {
		backoffTime = ReplicationBackoffMaxDelay
	}

	return backoffTime
}
