#define _GNU_SOURCE
#include "builtins.h"
#include "execute.h"
#include "trap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include "signal_map.h"

extern int last_status;
void list_signals(void);

/* Map signal names to numbers for trap builtin is now defined in signal_map.h */

char *trap_cmds[NSIG];
char *exit_trap_cmd;

/* Assign commands to run when specified signals are received. */
static void print_traps(void)
{
    if (exit_trap_cmd)
        printf("trap '%s' EXIT\n", exit_trap_cmd);
    for (int s = 1; s < NSIG; s++) {
        if (trap_cmds[s]) {
            const char *name = name_from_sig(s);
            if (name)
                printf("trap '%s' %s\n", trap_cmds[s], name);
            else
                printf("trap '%s' %d\n", trap_cmds[s], s);
        }
    }
}

/* Assign commands to run when specified signals are received. */
int builtin_trap(char **args)
{
    if (!args[1]) {
        print_traps();
        last_status = 0;
        return 1;
    }

    if (strcmp(args[1], "-p") == 0) {
        if (args[2]) {
            fprintf(stderr, "usage: trap -p\n");
            return 1;
        }
        print_traps();
        last_status = 0;
        return 1;
    }

    if (strcmp(args[1], "-l") == 0) {
        if (args[2]) {
            fprintf(stderr, "usage: trap -l\n");
            return 1;
        }
        list_signals();
        last_status = 0;
        return 1;
    }

    char *cmd = NULL;
    int idx = 1;
    if (args[2]) {
        cmd = args[1];
        idx = 2;
    }

    for (int i = idx; args[i]; i++) {
        if (strcasecmp(args[i], "EXIT") == 0 || strcmp(args[i], "0") == 0) {
            free(exit_trap_cmd);
            exit_trap_cmd = cmd ? strdup(cmd) : NULL;
            continue;
        }
        int sig = sig_from_name(args[i]);
        if (sig <= 0 || sig >= NSIG) {
            fprintf(stderr, "trap: invalid signal %s\n", args[i]);
            continue;
        }
        free(trap_cmds[sig]);
        trap_cmds[sig] = cmd ? strdup(cmd) : NULL;

        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = cmd ? trap_handler : SIG_DFL;
        sigaction(sig, &sa, NULL);
    }
    return 1;
}

/* Signal a loop to terminate after the current iteration. */
int builtin_break(char **args)
{
    int n = 1;
    if (args[1]) {
        char *end;
        errno = 0;
        long val = strtol(args[1], &end, 10);
        if (*end != '\0' || errno != 0 || val <= 0) {
            fprintf(stderr, "usage: break [N]\n");
            return 1;
        }
        n = (int)val;
    }
    if (n > loop_depth)
        n = loop_depth;
    loop_break = n;
    return 1;
}

/* Skip directly to the next iteration of the innermost loop. */
int builtin_continue(char **args)
{
    int n = 1;
    if (args[1]) {
        char *end;
        errno = 0;
        long val = strtol(args[1], &end, 10);
        if (*end != '\0' || errno != 0 || val <= 0) {
            fprintf(stderr, "usage: continue [N]\n");
            return 1;
        }
        n = (int)val;
    }
    if (n > loop_depth)
        n = loop_depth;
    loop_continue = n;
    return 1;
}

/* Free all registered trap command strings. */
void free_trap_cmds(void)
{
    for (int i = 0; i < NSIG; i++) {
        if (trap_cmds[i]) {
            free(trap_cmds[i]);
            trap_cmds[i] = NULL;
        }
    }
}

