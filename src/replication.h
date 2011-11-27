#ifndef _REPLICATION_H
#define _REPLICATION_H

#include "platform.h"

int add_replication_target(char *hostname, int port);
int parse_and_add_replication_target(char *target, int default_port);
void shutdown_replication();

#endif
