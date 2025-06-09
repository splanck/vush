#ifndef EXECUTE_H
#define EXECUTE_H

#include "parser.h"

int run_pipeline(Command *cmd, const char *line);
int run_command_list(Command *cmds, const char *line);

extern int loop_break;
extern int loop_continue;

#endif /* EXECUTE_H */
