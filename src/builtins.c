/*
 * Builtin command table and dispatch helpers.
 *
 * The table below lists every builtin command by name along with the
 * C function that implements it.  run_builtin() looks up a command in
 * this table using the first argument as the key and then invokes the
 * associated function.  If no entry matches, run_builtin() returns 0 so
 * the caller can treat the command as external.
 */
#include "builtins.h"
#include <string.h>

/* builtin_table is used by run_builtin and builtin_type */
extern int builtin_cd(char **);
extern int builtin_pushd(char **);
extern int builtin_popd(char **);
extern int builtin_dirs(char **);
extern int builtin_exit(char **);
extern int builtin_colon(char **);
extern int builtin_true(char **);
extern int builtin_false(char **);
extern int builtin_pwd(char **);
extern int builtin_jobs(char **);
extern int builtin_fg(char **);
extern int builtin_bg(char **);
extern int builtin_kill(char **);
extern int builtin_wait(char **);
extern int builtin_export(char **);
extern int builtin_readonly(char **);
extern int builtin_local(char **);
extern int builtin_unset(char **);
extern int builtin_history(char **);
extern int builtin_hash(char **);
extern int builtin_alias(char **);
extern int builtin_unalias(char **);
extern int builtin_read(char **);
extern int builtin_getopts(char **);
extern int builtin_eval(char **);
extern int builtin_printf(char **);
extern int builtin_exec(char **);
extern int builtin_command(char **);
extern int builtin_time(char **);
extern int builtin_umask(char **);
extern int builtin_return(char **);
extern int builtin_break(char **);
extern int builtin_continue(char **);
extern int builtin_shift(char **);
extern int builtin_let(char **);
extern int builtin_set(char **);
extern int builtin_trap(char **);
extern int builtin_test(char **);
extern int builtin_cond(char **);
extern int builtin_type(char **);
extern int builtin_source(char **);
extern int builtin_help(char **);

const struct builtin builtin_table[] = {
    {"cd", builtin_cd},
    {"pushd", builtin_pushd},
    {"popd", builtin_popd},
    {"printf", builtin_printf},
    {"dirs", builtin_dirs},
    {"exit", builtin_exit},
    {":", builtin_colon},
    {"true", builtin_true},
    {"false", builtin_false},
    {"pwd", builtin_pwd},
    {"jobs", builtin_jobs},
    {"fg", builtin_fg},
    {"bg", builtin_bg},
    {"kill", builtin_kill},
    {"wait", builtin_wait},
    {"export", builtin_export},
    {"readonly", builtin_readonly},
    {"local", builtin_local},
    {"unset", builtin_unset},
    {"history", builtin_history},
    {"hash", builtin_hash},
    {"alias", builtin_alias},
    {"unalias", builtin_unalias},
    {"read", builtin_read},
    {"return", builtin_return},
    {"break", builtin_break},
    {"continue", builtin_continue},
    {"shift", builtin_shift},
    {"getopts", builtin_getopts},
    {"let", builtin_let},
    {"set", builtin_set},
    {"trap", builtin_trap},
    {"test", builtin_test},
    {"[", builtin_test},
    {"[[", builtin_cond},
    {"type", builtin_type},
    {"command", builtin_command},
    {"eval", builtin_eval},
    {"exec", builtin_exec},
    {"time", builtin_time},
    {"umask", builtin_umask},
    {"source", builtin_source},
    {".", builtin_source},
    {"help", builtin_help},
    {NULL, NULL}
};

/*
 * Search the builtin table for a command matching args[0] and invoke
 * the associated function.  Returns the builtin's return value or 0 if
 * no builtin is found.
 */
int run_builtin(char **args)
{
    for (int i = 0; builtin_table[i].name; i++) {
        if (strcmp(args[0], builtin_table[i].name) == 0)
            return builtin_table[i].func(args);
    }
    return 0;
}

/*
 * Return a NULL-terminated list of all builtin command names.  The
 * returned array points into static storage and must not be freed.
 */
const char **get_builtin_names(void)
{
    static const char *names[sizeof(builtin_table) / sizeof(builtin_table[0])];
    int i = 0;
    for (; builtin_table[i].name; i++)
        names[i] = builtin_table[i].name;
    names[i] = NULL;
    return names;
}
