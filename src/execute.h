#ifndef EXECUTE_H
#define EXECUTE_H

/*
 * Executor interface.
 *
 * These declarations expose the high level execution entry points used by the
 * rest of the shell.  run_pipeline() and run_command_list() interpret the
 * parsed command structures produced by the parser.  Pipelines are expanded to
 * builtins, shell functions or external programs and control-flow constructs
 * like if/while/for are dispatched to the appropriate helpers.  The globals
 * loop_break and loop_continue are used by the break and continue builtins to
 * signal loop control to the executor.
 */

#include "parser.h"

/* Run the command or control structure CMD using LINE for job messages and
 * return the resulting exit status. */
int run_pipeline(Command *cmd, const char *line);

/* Execute the linked list CMDS forwarding LINE for tracing and return the
 * status of the last command. */
int run_command_list(Command *cmds, const char *line);

/* Set non-zero by builtins to break or continue the innermost loop. */
extern int loop_break;
extern int loop_continue;

#endif /* EXECUTE_H */
