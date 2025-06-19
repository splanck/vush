#ifndef SCRIPTARGS_H
#define SCRIPTARGS_H

#include "shell_state.h"

/* Number of positional parameters; updated before a function body executes. */
#define script_argc (shell_state.script_argc)
/* Null terminated array of parameters; swapped during function calls. */
#define script_argv (shell_state.script_argv)

#endif /* SCRIPTARGS_H */
