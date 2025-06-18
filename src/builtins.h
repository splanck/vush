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
int builtin_exit(char **args);
int builtin_colon(char **args);
int builtin_true(char **args);
int builtin_false(char **args);
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
int builtin_time_callback(int (*func)(void *), void *data, int posix);
int builtin_umask(char **args);
int builtin_ulimit(char **args);
int builtin_source(char **args);
const char **get_builtin_names(void);
const char *get_alias(const char *name);
void load_aliases(void);
void free_aliases(void);
typedef struct func_entry {
    char *name;
    char *text;
    Command *body;
    struct func_entry *next;
} FuncEntry;
void define_function(const char *name, Command *body, const char *text);
FuncEntry *find_function(const char *name);
Command *get_function(const char *name); /* deprecated */
void remove_function(const char *name);
void load_functions(void);
void free_functions(void);
void print_functions(void);
int builtin_local(char **args);
int builtin_break(char **args);
int builtin_continue(char **args);
int builtin_test(char **args);
int builtin_cond(char **args);
void list_signals(void);

extern char *trap_cmds[NSIG];
/* Maintains state across getopts calls. Must be cleared when script_argv
 * changes so it never points into freed memory. */
extern char *getopts_pos;
extern char *exit_trap_cmd;
void run_exit_trap(void);
void free_trap_cmds(void);
int builtin_trap(char **args);

#endif /* BUILTINS_H */
