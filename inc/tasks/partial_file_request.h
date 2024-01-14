#ifndef _PARTIAL_FILE_REQUEST_H_
#define _PARTIAL_FILE_REQUEST_H_

#include <stdlib.h>

#include "list.h"
#include "handler.h"

#include "file_request.h"

// typedef struct
// {
//     const char *ext;
//     const char *mime;
//     const char *addition;
// } file_type_t;
//  
// typedef list_t file_type_bank_t;
//  
// file_type_bank_t *file_type_bank_init(void);
// int file_type_bank_add(file_type_bank_t *const bank, file_type_t *type);
// const file_type_t *file_type_bank_get(file_type_bank_t *const bank,
//                                       const char *const path);
// void file_type_bank_free(file_type_bank_t **const bank);

handler_t partial_file_request_get(file_type_bank_t *const bank);

#endif

