/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Executor entry points.
 */

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
 * loop_break and loop_continue store the number of loop levels remaining
 * to break or continue, as set by the builtins of the same name.
 */

#include "parser.h"

/* Run the command or control structure CMD using LINE for job messages and
 * return the resulting exit status. */
int run_pipeline(Command *cmd, const char *line);

/* Execute the linked list CMDS forwarding LINE for tracing and return the
 * status of the last command. */
int run_command_list(Command *cmds, const char *line);

/* Remaining loop levels to break or continue (0 when inactive). */
extern int loop_break;
extern int loop_continue;
extern int loop_depth;

#endif /* EXECUTE_H */
