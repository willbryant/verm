#include "replication.h"
#include "str.h"

struct Replicator {
	char* hostname;
	int port;

	pthread_t thread;

	struct Replicator *next_replicator;
};

static pthread_mutex_t replication_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  replication_queue_cond  = PTHREAD_COND_INITIALIZER;
static int replication_shutdown = 0;
static int number_of_replicators = 0;
static struct Replicator *last_replicator = NULL;

void *replication_thread_entry(void *data) {
	struct Replicator *self = (struct Replicator *)data;
	fprintf(stdout, "replicating to '%s' port %d\n", self->hostname, self->port);
	if (pthread_mutex_lock(&replication_queue_mutex) < 0) return NULL;

	while (!replication_shutdown) {
		if (pthread_cond_wait(&replication_queue_cond, &replication_queue_mutex) < 0) return NULL;
		// TODO: push items
	}

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
