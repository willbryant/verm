#include "replication.h"
#include "str.h"

// FUTURE: make replication backoff exponential
#define BACKOFF_TIME 30
#define MAX_PATH_LENGTH 512

struct Replicator {
	char* hostname;
	int port;

	pthread_t thread;
	int need_resync;
	struct ReplicationFile *next_file;

	struct Replicator *next_replicator;
};

struct ReplicationFile {
	char location[MAX_PATH_LENGTH];
	char path[MAX_PATH_LENGTH];
	const char* encoding;
	time_t queued_at;

	struct ReplicationFile *next_file;
};

static pthread_mutex_t replication_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  replication_queue_cond  = PTHREAD_COND_INITIALIZER;
static int replication_shutdown = 0;
static int number_of_replicators = 0;
static struct Replicator *last_replicator = NULL;

int push_file(struct Replicator *replicator, struct ReplicationFile *file) {
	fprintf(stdout, "replicating %s to '%s' port %d\n", file->location, replicator->hostname, replicator->port);
	// TODO: implement HTTP posting
	return 0;
}

int resync(struct Replicator *replicator) {
	fprintf(stdout, "resyncing to '%s' port %d\n", replicator->hostname, replicator->port);
	// TODO: implement directory scanning and call push_file
	return 0;
}

void replication_backoff(struct Replicator *replicator) {
	struct timeval tv;
	struct timespec ts;
	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec + BACKOFF_TIME;
	ts.tv_nsec = 0;
	while (!replication_shutdown && !pthread_cond_timedwait(&replication_queue_cond, &replication_queue_mutex, &ts)) ;
}

void replication_free_queue(struct Replicator *replicator) {
	struct ReplicationFile *file;
	while (file = replicator->next_file) {
		replicator->next_file = file->next_file;
		free(file);
	}
}

void *replication_thread_entry(void *data) {
	int successful;
	struct Replicator *replicator = (struct Replicator *)data;
	struct ReplicationFile *file = NULL;

	fprintf(stdout, "replicating to '%s' port %d\n", replicator->hostname, replicator->port);
	if (pthread_mutex_lock(&replication_queue_mutex) < 0) return NULL;

	while (!replication_shutdown) {
		if (replicator->need_resync) {
			replication_free_queue(replicator);
			replicator->need_resync = 0;

			if (pthread_mutex_unlock(&replication_queue_mutex) < 0) return NULL;
			successful = resync(replicator);
			if (pthread_mutex_lock(&replication_queue_mutex) < 0) return NULL;

			if (!successful) {
				replicator->need_resync = 1;
				replication_backoff(replicator);
			}

		} else if (replicator->next_file) {
			file = replicator->next_file;
			if (pthread_mutex_unlock(&replication_queue_mutex) < 0) return NULL;
			successful = push_file(replicator, file);
			if (pthread_mutex_lock(&replication_queue_mutex) < 0) return NULL;

			if (successful) {
				// move on to the next file
				replicator->next_file = file->next_file;
				free(file);
			} else {
				replication_backoff(replicator);
			}
		} else {
			if (pthread_cond_wait(&replication_queue_cond, &replication_queue_mutex) < 0) return NULL;
			continue;
		}
	}

	replication_free_queue(replicator);

	if (pthread_mutex_unlock(&replication_queue_mutex) < 0) return NULL;
	return NULL;
}

int add_replication_target(char *hostname, int port) {
	struct Replicator* replicator = (struct Replicator *)malloc(sizeof(struct Replicator));
	if (!replicator) return -1;

	replicator->hostname = strdup(hostname);
	if (!replicator->hostname) {
		free(replicator);
		return -1;
	}
	replicator->port = port;
	replicator->next_file = NULL;
	replicator->next_replicator = last_replicator;
	if (pthread_create(&replicator->thread, NULL, &replication_thread_entry, replicator) < 0) return -1;

	last_replicator = replicator;
	number_of_replicators++;
	return 0;
}

int parse_and_add_replication_target(char *target, int default_port) {
	int ret, port;
	char *colon = strchr(target, ':');
	if (colon) {
		port = atoi_or_default(colon + 1, 0);
		if (!port) return EINVAL;
		*colon = 0;
		ret = add_replication_target(target, port);
		*colon = ':';
		return ret;
	} else {
		return add_replication_target(target, default_port);
	}
}

void add_replication_file(const char *location, const char *path, const char *encoding) {
	struct Replicator *replicator;
	pthread_mutex_lock(&replication_queue_mutex);

	for (replicator = last_replicator; replicator != NULL; replicator = replicator->next_replicator) {
		if (replicator->need_resync) continue;

		struct ReplicationFile *file = malloc(sizeof(struct ReplicationFile));
		if (file) {
			strncpy(file->location, location, sizeof(file->location));
			strncpy(file->path, path, sizeof(file->path));
			file->encoding = encoding; // static strings
			time(&file->queued_at);
			file->next_file = NULL;

			if (replicator->next_file) {
				replicator->next_file->next_file = file;
			} else {
				replicator->next_file = file;
			}
		} else {
			// since we haven't added the file to the queue for this replicator, set the flag asking for a complete resync
			// nb. we can't free its queue at this point because although we hold the queue mutex, the replication thread
			// may be using one of the records from its queue without holding the mutex - which is only for mutating the queue
			replicator->need_resync = 1;
		}
	}

	pthread_cond_broadcast(&replication_queue_cond);
	pthread_mutex_unlock(&replication_queue_mutex);
}

void shutdown_replicator(struct Replicator *replicator) {
	pthread_mutex_lock(&replication_queue_mutex);
	replication_shutdown = 1;
	pthread_cond_broadcast(&replication_queue_cond);
	pthread_mutex_unlock(&replication_queue_mutex);
	pthread_join(replicator->thread, NULL);
	free(replicator->hostname);
	free(replicator);
}

void shutdown_replication() {
	struct Replicator *replicator, *next;
	
	replicator = last_replicator;
	while (replicator) {
		next = replicator->next_replicator;
		shutdown_replicator(replicator);
		replicator = next;
	}
}
