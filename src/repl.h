/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Read-eval-print loop.
 */

#ifndef REPL_H
#define REPL_H
#include <stdio.h>

/*
 * Start the shell's read-eval-print loop using INPUT as the source of
 * commands.  When INPUT is stdin the shell runs interactively.
 */
void repl_loop(FILE *input);

#endif /* REPL_H */
