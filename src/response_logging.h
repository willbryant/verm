#ifndef _RESPONSE_LOGGING_H
#define _RESPONSE_LOGGING_H

#include "platform.h"
#include "microhttpd.h"

int responded(struct MHD_Connection* connection);
int log_response(struct MHD_Connection* connection);

#endif
