/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#ifndef BUILTINS_H
#define BUILTINS_H

int run_builtin(char **args);
const char *get_alias(const char *name);

#endif /* BUILTINS_H */
