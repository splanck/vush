#ifndef SIGNAL_MAP_H
#define SIGNAL_MAP_H

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static const struct { const char *n; int v; } sig_map[] = {
    {"INT", SIGINT}, {"TERM", SIGTERM}, {"HUP", SIGHUP},
#ifdef SIGQUIT
    {"QUIT", SIGQUIT},
#endif
#ifdef SIGUSR1
    {"USR1", SIGUSR1},
#endif
#ifdef SIGUSR2
    {"USR2", SIGUSR2},
#endif
#ifdef SIGCHLD
    {"CHLD", SIGCHLD},
#endif
#ifdef SIGCONT
    {"CONT", SIGCONT},
#endif
#ifdef SIGSTOP
    {"STOP", SIGSTOP},
#endif
#ifdef SIGALRM
    {"ALRM", SIGALRM},
#endif
    {NULL, 0}
};

static inline int sig_from_name(const char *name)
{
    if (!name || !*name)
        return -1;
    if (isdigit((unsigned char)name[0]))
        return atoi(name);
    if (strncasecmp(name, "SIG", 3) == 0)
        name += 3;
    for (int i = 0; sig_map[i].n; i++) {
        if (strcasecmp(name, sig_map[i].n) == 0)
            return sig_map[i].v;
    }
    return -1;
}

static inline const char *name_from_sig(int sig)
{
    for (int i = 0; sig_map[i].n; i++) {
        if (sig_map[i].v == sig)
            return sig_map[i].n;
    }
    return NULL;
}

#endif /* SIGNAL_MAP_H */
