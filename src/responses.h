#ifndef _RESPONSES_H
#define _RESPONSES_H

#include "platform.h"
#include "microhttpd.h"

int send_upload_page_response(struct MHD_Connection* connection);
int send_file_not_found_response(struct MHD_Connection* connection);
int send_not_modified_response(struct MHD_Connection* connection, const char* etag);
int send_redirected_response(struct MHD_Connection* connection, char* location);
int send_created_response(struct MHD_Connection* connection, char* location);
int send_conflict_response(struct MHD_Connection* connection);

#endif
