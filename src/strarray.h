#ifndef STRARRAY_H
#define STRARRAY_H

#include <stddef.h>

typedef struct {
    char **items;
    int count;
    int capacity;
} StrArray;

void strarray_init(StrArray *arr);
int strarray_push(StrArray *arr, char *str);
char **strarray_finish(StrArray *arr); /* returns NULL terminated array */
void strarray_free(char **arr);
void strarray_release(StrArray *arr);

#endif /* STRARRAY_H */
