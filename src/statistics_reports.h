#ifndef STATISTICS_REPORTS_H
#define STATISTICS_REPORTS_H

#include "microhttpd.h"

char *create_log_statistics_string(struct MHD_Connection *connection, const char *suffix);
char *create_statistics_string(struct MHD_Connection *connection);

#endif
