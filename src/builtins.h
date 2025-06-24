/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Builtin command declarations.
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include "parser.h"
#include "list.h"
#include <signal.h>
#ifndef NSIG
# ifdef _NSIG
#  define NSIG _NSIG
# else
#  include <unistd.h>
int get_nsig(void);
#  define NSIG (get_nsig())
# endif
#endif

struct builtin {
    const char *name;
    int (*func)(char **);
};

/*
 * Builtin registration list.  Each entry provides an identifier, the
 * command name and the implementing function.  The list is consumed with
 * different definitions of DEF_BUILTIN to generate enums, prototypes and
 * tables.
 */
#define DEF_BUILTIN(id, name, func) int func(char **);
#include "builtins_list.h"
#undef DEF_BUILTIN

enum builtin_id {
#define DEF_BUILTIN(id, name, func) BI_##id,
#include "builtins_list.h"
#undef DEF_BUILTIN
    BI_COUNT
};

extern const struct builtin builtin_table[BI_COUNT];

int run_builtin(char **args);
int builtin_time_callback(int (*func)(void *), void *data, int posix);
const char **get_builtin_names(void);
const char *get_alias(const char *name);
void load_aliases(void);
void free_aliases(void);
typedef struct func_entry {
    char *name;
    char *text;
    Command *body;
    ListNode node;
} FuncEntry;
void define_function(const char *name, Command *body, const char *text);
FuncEntry *find_function(const char *name);
Command *get_function(const char *name); /* deprecated */
void remove_function(const char *name);
void load_functions(void);
void free_functions(void);
void print_functions(void);
void list_signals(void);

extern char **trap_cmds;
void init_signal_handling(void);
/* Maintains state across getopts calls. Must be cleared when script_argv
 * changes so it never points into freed memory. */
extern char *getopts_pos;
extern char *exit_trap_cmd;
void run_exit_trap(void);
void free_trap_cmds(void);

#endif /* BUILTINS_H */
