#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>

static char *(*real_strdup)(const char*) = NULL;
static long count = 0;

__attribute__((constructor))
static void init(void) {
    real_strdup = dlsym(RTLD_NEXT, "strdup");
}

char *strdup(const char *s) {
    count++;
    char *env = getenv("STRDUP_FAIL_AT");
    if (env && atol(env) == count) {
        errno = ENOMEM;
        return NULL;
    }
    return real_strdup ? real_strdup(s) : NULL;
}
