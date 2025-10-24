#ifndef _MISC_H_
#define _MISC_H_

#include <stdlib.h>
#include <sys/time.h>

size_t get_time_diff_ms(const struct timeval *begin, const struct timeval *end);

#endif // _MISC_H_

