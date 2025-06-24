/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Execution helpers for control flow.
 */

#ifndef CONTROL_H
#define CONTROL_H

#include "parser.h"

int exec_if(Command *cmd, const char *line);
int exec_while(Command *cmd, const char *line);
int exec_until(Command *cmd, const char *line);
int exec_for(Command *cmd, const char *line);
int exec_select(Command *cmd, const char *line);
int exec_for_arith(Command *cmd, const char *line);
int exec_case(Command *cmd, const char *line);
int exec_subshell(Command *cmd, const char *line);
int exec_cond(Command *cmd, const char *line);
int exec_arith(Command *cmd, const char *line);
int exec_group(Command *cmd, const char *line);

#endif /* CONTROL_H */
