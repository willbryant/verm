#include "responses.h"

#define HTTP_404_PAGE "<!DOCTYPE html><html><head><title>Verm - File not found</title></head><body>File not found</body></html>"
#define UPLOAD_PAGE "<!DOCTYPE html><html><head><title>Verm - Upload</title></head><body>" \
                    "<form method='post' enctype='multipart/form-data'>" \
                    "<input type='hidden' name='redirect' value='1'/>" /* redirect instead of returning 201 (as APIs want) */ \
                    "<input type='file' name='uploaded_file'/>" \
                    "<input type='submit' value='Upload'/>" \
                    "</form>" \
                    "</body></html>\n"
#define CREATED_PAGE "Resource created\n"
#define REDIRECT_PAGE "You are being redirected\n"

int send_static_page_response(struct MHD_Connection* connection, unsigned int status_code, char* page) {
	struct MHD_Response* response;
	int ret;
	
	response = MHD_create_response_from_buffer(strlen(page), page, MHD_RESPMEM_PERSISTENT);
	ret = MHD_queue_response(connection, status_code, response); // cleanly returns MHD_NO if response was NULL for any reason
	MHD_destroy_response(response); // does nothing if response was NULL for any reason
	return ret;
}

int send_upload_page_response(struct MHD_Connection* connection) {
	return send_static_page_response(connection, MHD_HTTP_OK, UPLOAD_PAGE);
}

int send_file_not_found_response(struct MHD_Connection* connection) {
	return send_static_page_response(connection, MHD_HTTP_NOT_FOUND, HTTP_404_PAGE);
}

int send_not_modified_response(struct MHD_Connection* connection, const char* etag) {
	struct MHD_Response* response;
	int ret;
	
	response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
	ret = MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, etag) &&
	      MHD_queue_response(connection, MHD_HTTP_NOT_MODIFIED, response); // cleanly returns MHD_NO if response was NULL for any reason
	MHD_destroy_response(response); // does nothing if response was NULL for any reason
	return ret;
}

int send_redirect(struct MHD_Connection* connection, unsigned int status_code, char* location, char* page) {
	struct MHD_Response* response;
	int ret;
	
	response = MHD_create_response_from_buffer(strlen(page), page, MHD_RESPMEM_PERSISTENT);
	ret = MHD_add_response_header(response, "Location", location) &&
	      MHD_queue_response(connection, status_code, response); // cleanly returns MHD_NO if response was NULL for any reason
	MHD_destroy_response(response); // does nothing if response was NULL for any reason
	return ret;
}

int send_redirected_response(struct MHD_Connection* connection, char* location) {
	send_redirect(connection, MHD_HTTP_SEE_OTHER, location, REDIRECT_PAGE);
}

int send_see_other_response(struct MHD_Connection* connection, char* location) {
	send_redirect(connection, MHD_HTTP_CREATED, location, CREATED_PAGE);
}
