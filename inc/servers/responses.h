#ifndef _RESPONSES_H_
#define _RESPONSES_H_

#include "codes.h"

int send_internal_error(int socket, http_response_code_t code, const char *message);
int send_refuse_connection(int socket);

#endif // _RESPONSES_H_

