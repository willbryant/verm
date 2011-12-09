#ifndef _REPLICATION_H
#define _REPLICATION_H

#include "platform.h"

int add_replication_target(char *hostname, int port);
int parse_and_add_replication_target(char *target, int default_port);
void add_replication_file(const char *location, const char *path, const char *encoding);
void shutdown_replication();

#endif
