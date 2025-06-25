/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Dynamic array of strings helpers.
 */

#include "strarray.h"
#include <stdlib.h>

/* Prepare ARR for use. */
void strarray_init(StrArray *arr) {
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

/* Append STR to ARR.  Returns 0 on success or -1 on allocation failure. */
int strarray_push(StrArray *arr, char *str) {
    if (arr->count + 1 > arr->capacity) {
        int newcap = arr->capacity ? arr->capacity * 2 : 4;
        char **tmp = realloc(arr->items, (size_t)newcap * sizeof(char *));
        if (!tmp)
            return -1;
        arr->items = tmp;
        arr->capacity = newcap;
    }
    arr->items[arr->count++] = str;
    return 0;
}

/* Convert ARR into a NULL terminated array owned by the caller. */
char **strarray_finish(StrArray *arr) {
    if (strarray_push(arr, NULL) == -1) {
        strarray_release(arr);
        return NULL;
    }
    char **res = arr->items;
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
    return res;
}

/* Free an array returned from strarray_finish. */
void strarray_free(char **arr) {
    if (!arr)
        return;
    for (char **p = arr; *p; p++)
        free(*p);
    free(arr);
}

/* Release memory associated with ARR. */
void strarray_release(StrArray *arr) {
    if (!arr || !arr->items)
        return;
    for (int i = 0; i < arr->count; i++)
        free(arr->items[i]);
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}
