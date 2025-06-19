/*
 * Error handling macros used across the shell.
 */
#ifndef VUSH_ERROR_H
#define VUSH_ERROR_H

#include <stdio.h>

/*
 * Check memory allocation and return -1 on failure while printing a
 * diagnostic with perror().
 */
#define CHECK_ALLOC_RET(ptr, retval) \
    do { \
        if (!(ptr)) { \
            perror("malloc"); \
            return retval; \
        } \
    } while (0)

#define CHECK_ALLOC(ptr) CHECK_ALLOC_RET(ptr, -1)

/*
 * Return -1 when COND is true after reporting MSG with perror().
 */
#define RETURN_IF_ERR_RET(cond, msg, retval) \
    do { \
        if (cond) { \
            perror(msg); \
            return retval; \
        } \
    } while (0)

#define RETURN_IF_ERR(cond, msg) RETURN_IF_ERR_RET(cond, msg, -1)

#endif /* VUSH_ERROR_H */
