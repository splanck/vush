/*
 * This header centralizes common definitions and portability helpers.
 * It provides fallbacks like PATH_MAX when the system lacks them.
 */
#ifndef VUSH_COMMON_H
#define VUSH_COMMON_H

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#endif /* VUSH_COMMON_H */
