#include <unistd.h>
#include <signal.h>

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
