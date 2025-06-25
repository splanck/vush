/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Portable signal helper functions.
 */

#include <unistd.h>
#include <signal.h>

/* Return the number of supported signals for the current system. */
int get_nsig(void) {
#ifdef NSIG
    return NSIG;
#elif defined(_NSIG)
    return _NSIG;
#else
#ifdef _SC_NSIG
    long n = sysconf(_SC_NSIG);
    if (n > 0)
        return (int)n;
#endif
    return 128;
#endif
}
