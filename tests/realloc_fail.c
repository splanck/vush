#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>

static void *(*real_realloc)(void *, size_t) = NULL;
static long count = 0;

void *realloc(void *ptr, size_t size) {
    if (!real_realloc)
        real_realloc = dlsym(RTLD_NEXT, "realloc");
    count++;
    char *env = getenv("REALLOC_FAIL_AT");
    if (env && atol(env) == count) {
        errno = ENOMEM;
        return NULL;
    }
    return real_realloc(ptr, size);
}
