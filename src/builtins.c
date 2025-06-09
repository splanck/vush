/*
 * Builtin command table and dispatch helpers.
 */
#include "builtins.h"
#include <string.h>

/* builtin_table is used by run_builtin and builtin_type */
extern int builtin_cd(char **);
extern int builtin_pushd(char **);
extern int builtin_popd(char **);
extern int builtin_dirs(char **);
extern int builtin_exit(char **);
extern int builtin_pwd(char **);
extern int builtin_jobs(char **);
extern int builtin_fg(char **);
extern int builtin_bg(char **);
extern int builtin_kill(char **);
extern int builtin_export(char **);
extern int builtin_unset(char **);
extern int builtin_history(char **);
extern int builtin_alias(char **);
extern int builtin_unalias(char **);
extern int builtin_read(char **);
extern int builtin_getopts(char **);
extern int builtin_eval(char **);
extern int builtin_return(char **);
extern int builtin_shift(char **);
extern int builtin_let(char **);
extern int builtin_set(char **);
extern int builtin_trap(char **);
extern int builtin_test(char **);
extern int builtin_type(char **);
extern int builtin_source(char **);
extern int builtin_help(char **);

const struct builtin builtin_table[] = {
    {"cd", builtin_cd},
    {"pushd", builtin_pushd},
    {"popd", builtin_popd},
    {"dirs", builtin_dirs},
    {"exit", builtin_exit},
    {"pwd", builtin_pwd},
    {"jobs", builtin_jobs},
    {"fg", builtin_fg},
    {"bg", builtin_bg},
    {"kill", builtin_kill},
    {"export", builtin_export},
    {"unset", builtin_unset},
    {"history", builtin_history},
    {"alias", builtin_alias},
    {"unalias", builtin_unalias},
    {"read", builtin_read},
    {"return", builtin_return},
    {"shift", builtin_shift},
    {"getopts", builtin_getopts},
    {"let", builtin_let},
    {"set", builtin_set},
    {"trap", builtin_trap},
    {"test", builtin_test},
    {"[", builtin_test},
    {"type", builtin_type},
    {"eval", builtin_eval},
    {"source", builtin_source},
    {".", builtin_source},
    {"help", builtin_help},
    {NULL, NULL}
};

int run_builtin(char **args)
{
    for (int i = 0; builtin_table[i].name; i++) {
        if (strcmp(args[0], builtin_table[i].name) == 0)
            return builtin_table[i].func(args);
    }
    return 0;
}

const char **get_builtin_names(void)
{
    static const char *names[sizeof(builtin_table) / sizeof(builtin_table[0])];
    int i = 0;
    for (; builtin_table[i].name; i++)
        names[i] = builtin_table[i].name;
    names[i] = NULL;
    return names;
}
