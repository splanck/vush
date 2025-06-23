#define _GNU_SOURCE
#include "strarray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(void) {
    StrArray arr;
    strarray_init(&arr);
    for (int i = 0; i < 4; i++) {
        char *s = strdup("x");
        if (!s || strarray_push(&arr, s) == -1) {
            perror("alloc");
            free(s);
            strarray_release(&arr);
            return 1;
        }
    }

    char **res = strarray_finish(&arr);
    if (res) {
        fprintf(stderr, "unexpected success\n");
        strarray_free(res);
        return 1;
    }

    if (arr.items || arr.count != 0 || arr.capacity != 0) {
        fprintf(stderr, "array not released\n");
        strarray_release(&arr);
        return 1;
    }

    perror("realloc");
    return 0;
}
