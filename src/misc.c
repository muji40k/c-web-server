#include "misc.h"

size_t get_time_diff_ms(const struct timeval *begin, const struct timeval *end) {
    return (end->tv_sec - begin->tv_sec) * 1000
           + (end->tv_usec - begin->tv_usec) / 1000;
}

