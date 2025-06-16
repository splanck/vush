#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>

static void *(*real_calloc)(size_t, size_t) = NULL;
static long count = 0;

void *calloc(size_t nmemb, size_t size) {
    if (!real_calloc)
        real_calloc = dlsym(RTLD_NEXT, "calloc");
    count++;
    char *env = getenv("CALLOC_FAIL_AT");
    if (env && atol(env) == count) {
        errno = ENOMEM;
        return NULL;
    }
    return real_calloc(nmemb, size);
}

