#ifndef _REPLICATION_H
#define _REPLICATION_H

#include "platform.h"

int add_replication_target(char *hostname, char *service);
int parse_and_add_replication_target(char *target, char *default_service);
void add_replication_file(const char *location, const char *path, const char *encoding);
void shutdown_replication();

#endif
