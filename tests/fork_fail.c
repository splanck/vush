#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>

static pid_t (*real_fork)(void) = NULL;
static long count = 0;

pid_t fork(void) {
    if (!real_fork)
        real_fork = dlsym(RTLD_NEXT, "fork");
    count++;
    char *env = getenv("FORK_FAIL_AT");
    if (env && atol(env) == count) {
        errno = EAGAIN;
        return -1;
    }
    return real_fork();
}
