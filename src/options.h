/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Shell option access macros.
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include "shell_state.h"

#define opt_errexit   (shell_state.opt_errexit)
#define opt_nounset   (shell_state.opt_nounset)
#define opt_xtrace    (shell_state.opt_xtrace)
#define opt_verbose   (shell_state.opt_verbose)
#define opt_pipefail  (shell_state.opt_pipefail)
#define opt_ignoreeof (shell_state.opt_ignoreeof)
#define opt_noclobber (shell_state.opt_noclobber)
#define opt_noexec    (shell_state.opt_noexec)
#define opt_noglob    (shell_state.opt_noglob)
#define opt_allexport (shell_state.opt_allexport)
#define opt_monitor   (shell_state.opt_monitor)
#define opt_notify    (shell_state.opt_notify)
#define opt_privileged (shell_state.opt_privileged)
#define opt_posix     (shell_state.opt_posix)
#define opt_onecmd    (shell_state.opt_onecmd)
#define opt_hashall   (shell_state.opt_hashall)
#define opt_keyword   (shell_state.opt_keyword)
#define current_lineno (shell_state.current_lineno)
#define parent_pid    (shell_state.parent_pid)

#endif /* OPTIONS_H */
