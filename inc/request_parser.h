#ifndef _REQUEST_PARSER_H_
#define _REQUEST_PARSER_H_

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#define ERROR_REQUEST_PARSER_NULL            1
#define ERROR_REQUEST_PARSER_ZERO_SIZE       1
#define ERROR_REQUEST_PARSER_ALLOCATION      1
#define ERROR_REQUEST_PARSER_INVALID         1
#define ERROR_REQUEST_PARSER_NOT_PRESENT     1
#define ERROR_REQUEST_PARSER_INVALID_SOCKET  1
#define ERROR_REQUEST_PARSER_READ_ERROR      1
#define ERROR_REQUEST_PARSER_EMPTY_READ      1
#define ERROR_REQUEST_PARSER_CLEAR           1
#define ERROR_REQUEST_PARSER_INCORRECT       1

typedef struct _request request_t;

typedef struct
{
    const char *method;
    const char *path;
    const char *version;
} request_title_t;

request_t *request_blank(const size_t size);
request_t *request_read(const int socket);
int request_read_exist(request_t *request, const int socket);
const request_title_t *request_title(const request_t *const request);
const char *request_at(const request_t *const request, const char *const header);
const char *request_pararmeters_at(const request_t *const request, const char *const parameter);
const char *request_body(const request_t *const request);
void request_free(request_t **const request);

#endif

