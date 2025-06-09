/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include "parser.h"

int run_builtin(char **args);
int builtin_type(char **args);
int builtin_dirs(char **args);
int builtin_read(char **args);
const char **get_builtin_names(void);
const char *get_alias(const char *name);
void load_aliases(void);
void free_aliases(void);
void define_function(const char *name, Command *body, const char *text);
Command *get_function(const char *name);
void load_functions(void);
void free_functions(void);
const char *get_shell_var(const char *name);
void set_shell_var(const char *name, const char *value);
void unset_shell_var(const char *name);
void free_shell_vars(void);

#endif /* BUILTINS_H */
