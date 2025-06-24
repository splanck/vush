/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Builtin command table and dispatch helpers.
 */

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

const struct builtin builtin_table[BI_COUNT] = {
#define DEF_BUILTIN(id, name, func) [BI_##id] = { name, func },
#include "builtins_list.h"
#undef DEF_BUILTIN
};

/*
 * Search the builtin table for a command matching args[0] and invoke
 * the associated function.  Returns the builtin's return value or 0 if
 * no builtin is found.
 */
int run_builtin(char **args)
{
    for (int i = 0; i < BI_COUNT; i++) {
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
    static const char *names[BI_COUNT + 1];
    for (int i = 0; i < BI_COUNT; i++)
        names[i] = builtin_table[i].name;
    names[BI_COUNT] = NULL;
    return names;
}
