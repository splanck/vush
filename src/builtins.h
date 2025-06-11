/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include "parser.h"
#include <signal.h>
#ifndef NSIG
#define NSIG _NSIG
#endif

struct builtin {
    const char *name;
    int (*func)(char **);
};

extern const struct builtin builtin_table[];

int run_builtin(char **args);
int builtin_type(char **args);
int builtin_dirs(char **args);
int builtin_read(char **args);
int builtin_getopts(char **args);
int builtin_eval(char **args);
int builtin_printf(char **args);
int builtin_echo(char **args);
int builtin_history(char **args);
int builtin_fc(char **args);
int builtin_exec(char **args);
int builtin_command(char **args);
int builtin_time(char **args);
int builtin_times(char **args);
const char **get_builtin_names(void);
const char *get_alias(const char *name);
void load_aliases(void);
void free_aliases(void);
void define_function(const char *name, Command *body, const char *text);
Command *get_function(const char *name);
void remove_function(const char *name);
void load_functions(void);
void free_functions(void);
int builtin_local(char **args);
int builtin_break(char **args);
int builtin_continue(char **args);
int builtin_cond(char **args);

extern char *trap_cmds[NSIG];
extern char *exit_trap_cmd;
void run_exit_trap(void);
int builtin_trap(char **args);

#endif /* BUILTINS_H */
