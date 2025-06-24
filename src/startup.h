/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Startup file processing.
 */

#ifndef STARTUP_H
#define STARTUP_H
#include <stdio.h>
int process_rc_file(const char *path, FILE *input);
int process_startup_file(FILE *input);
void run_command_string(const char *cmd);
#endif /* STARTUP_H */
