/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Startup file processing.
 */

#ifndef STARTUP_H
#define STARTUP_H
#include <stdio.h>

/*
 * Source RC file located at PATH using INPUT for parse context.
 * Returns non-zero if any commands were executed.
 */
int process_rc_file(const char *path, FILE *input);

/*
 * Locate and source the default startup file (~/.vushrc) if present.
 */
int process_startup_file(FILE *input);

/*
 * Execute CMD provided via the -c command line option.
 */
void run_command_string(const char *cmd);
#endif /* STARTUP_H */
