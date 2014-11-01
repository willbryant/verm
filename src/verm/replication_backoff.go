package verm

import "time"

func backoffTime(failures uint) time.Duration {
	if failures < 2 {
		return BACKOFF_BASE_TIME*time.Second
	}

	backoff_time := BACKOFF_BASE_TIME*(2 << (failures - 3))

	if backoff_time > BACKOFF_MAX_TIME {
		return BACKOFF_MAX_TIME*time.Second
	} else {
		return time.Duration(backoff_time)*time.Second
	}
}
