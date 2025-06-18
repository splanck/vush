#ifndef OPTIONS_H
#define OPTIONS_H

#include <sys/types.h>

extern int opt_errexit;
extern int opt_nounset;
extern int opt_xtrace;
extern int opt_verbose;
extern int opt_pipefail;
extern int opt_ignoreeof;
extern int opt_noclobber;
extern int opt_noexec;
extern int opt_noglob;
extern int opt_allexport;
extern int opt_monitor;
extern int opt_notify;
extern int opt_privileged;
extern int opt_posix;
extern int opt_onecmd;
extern int opt_hashall;
extern int opt_keyword;
extern int current_lineno;
extern pid_t parent_pid;

#endif /* OPTIONS_H */
