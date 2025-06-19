#ifndef SHELL_STATE_H
#define SHELL_STATE_H

#include <sys/types.h>

/*
 * Central structure storing shell runtime state.
 */
typedef struct ShellState {
    int last_status;
    int param_error;
    int script_argc;
    char **script_argv;
    int opt_errexit;
    int opt_nounset;
    int opt_xtrace;
    int opt_verbose;
    int opt_pipefail;
    int opt_ignoreeof;
    int opt_noclobber;
    int opt_noexec;
    int opt_noglob;
    int opt_allexport;
    int opt_monitor;
    int opt_notify;
    int opt_privileged;
    int opt_posix;
    int opt_onecmd;
    int opt_hashall;
    int opt_keyword;
    int current_lineno;
    pid_t parent_pid;
} ShellState;

extern ShellState shell_state;

#define last_status  (shell_state.last_status)
#define param_error  (shell_state.param_error)
#define script_argc  (shell_state.script_argc)
#define script_argv  (shell_state.script_argv)

#endif /* SHELL_STATE_H */
