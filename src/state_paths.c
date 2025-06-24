/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Paths used for persistent shell state.
 */

#define _GNU_SOURCE
#include "state_paths.h"
#include "util.h"

char *get_alias_file(void)
{
    return make_user_path("VUSH_ALIASFILE", NULL, ".vush_aliases");
}

char *get_func_file(void)
{
    return make_user_path("VUSH_FUNCFILE", NULL, ".vush_funcs");
}

char *get_history_file(void)
{
    return make_user_path("VUSH_HISTFILE", "HISTFILE", ".vush_history");
}

