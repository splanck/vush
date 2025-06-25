/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Dynamic string array.
 */

#ifndef STRARRAY_H
#define STRARRAY_H

#include <stddef.h>

typedef struct {
    char **items;
    int count;
    int capacity;
} StrArray;

/* Initialize ARR so it can accept strings. */
void strarray_init(StrArray *arr);
/* Append STR to ARR growing the backing store as needed. */
int strarray_push(StrArray *arr, char *str);
/* Finish building ARR and return a NULL terminated array. */
char **strarray_finish(StrArray *arr);
/* Free an array created by strarray_finish. */
void strarray_free(char **arr);
/* Release all memory held by ARR without returning an array. */
void strarray_release(StrArray *arr);

#endif /* STRARRAY_H */
