/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Variable management builtins.
 */

/*
 * This module implements the builtins used to manipulate shell variables and
 * arrays.  A small linked list stores all shell variables so that both
 * builtins and expansion code share the same state.  Routines here are
 * responsible for looking up variables, creating or deleting them and keeping
 * array values in sync.  Builtin commands such as `set`, `export`, `read` and
 * `getopts` rely on these helpers to manage their arguments and update the
 * environment.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "builtin_options.h"
#include "history.h"
#include "parser.h"
#include "execute.h"
#include "func_exec.h"
#include "scriptargs.h"
#include "arith.h"
#include "vars.h"
#include "options.h"
#include "lineedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include "util.h"
#include "assignment_utils.h"



static void print_option(const char *name, int enabled)
{
    printf("%s\t%s\n", name, enabled ? "on" : "off");
}

/* List all shell options in a `name\ton|off` format. */
static void list_shell_options(void)
{
    print_option("allexport", opt_allexport);
    print_option("errexit", opt_errexit);
    print_option("hashall", opt_hashall);
    print_option("ignoreeof", opt_ignoreeof);
    print_option("keyword", opt_keyword);
    print_option("monitor", opt_monitor);
    print_option("noclobber", opt_noclobber);
    print_option("noexec", opt_noexec);
    print_option("noglob", opt_noglob);
    print_option("notify", opt_notify);
    print_option("nounset", opt_nounset);
    print_option("onecmd", opt_onecmd);
    print_option("pipefail", opt_pipefail);
    print_option("privileged", opt_privileged);
    print_option("posix", opt_posix);
    print_option("emacs", lineedit_mode == LINEEDIT_EMACS);
    print_option("vi", lineedit_mode == LINEEDIT_VI);
    print_option("verbose", opt_verbose);
    print_option("xtrace", opt_xtrace);
}


/*
 * Implements the `shift` builtin which discards the first n positional
 * parameters.  On error an explanatory message is printed and 1 is returned.
 * Success also returns 1 since builtins signal the shell to continue.
 */
int builtin_shift(char **args) {
    int n = 1;
    if (args[1]) {
        int val;
        if (parse_positive_int(args[1], &val) < 0 || val < 0) {
            fprintf(stderr, "usage: shift [n]\n");
            return 1;
        }
        n = val;
    }

    if (n > script_argc) {
        fprintf(stderr, "shift: shift count out of range\n");
        return 1;
    }

    for (int i = 1; i <= n; i++)
        free(script_argv[i]);
    for (int i = n + 1; i <= script_argc; i++)
        script_argv[i - n] = script_argv[i];
    script_argc -= n;
    script_argv[script_argc + 1] = NULL;
    getopts_pos = NULL; /* shifting invalidates cached $@ state for getopts */
    return 1;
}

/*
 * Change shell execution options such as -e or -u.  Unrecognised options
 * result in an error message.  The function always returns 1.
 */
int builtin_set(char **args) {
    if (!args[1]) {
        print_shell_vars();
        print_functions();
        return 1;
    }

    if ((strcmp(args[1], "-o") == 0 || strcmp(args[1], "+o") == 0) && !args[2]) {
        list_shell_options();
        return 1;
    }

    int i = 1;
    for (; args[i]; i++) {
        if (strcmp(args[i], "--") == 0) {
            i++;
            break;
        }
        if (strcmp(args[i], "-e") == 0)
            opt_errexit = 1;
        else if (strcmp(args[i], "-u") == 0)
            opt_nounset = 1;
        else if (strcmp(args[i], "-x") == 0)
            opt_xtrace = 1;
        else if (strcmp(args[i], "-v") == 0)
            opt_verbose = 1;
        else if (strcmp(args[i], "-n") == 0)
            opt_noexec = 1;
        else if (strcmp(args[i], "-f") == 0)
            opt_noglob = 1;
        else if (strcmp(args[i], "-C") == 0)
            opt_noclobber = 1;
        else if (strcmp(args[i], "-a") == 0)
            opt_allexport = 1;
        else if (strcmp(args[i], "-b") == 0)
            opt_notify = 1;
        else if (strcmp(args[i], "-m") == 0)
            opt_monitor = 1;
        else if (strcmp(args[i], "-p") == 0)
            opt_privileged = 1;
        else if (strcmp(args[i], "-t") == 0)
            opt_onecmd = 1;
        else if (strcmp(args[i], "-h") == 0)
            opt_hashall = 1;
        else if (strcmp(args[i], "-k") == 0)
            opt_keyword = 1;
        else if (strcmp(args[i], "-o") == 0 && args[i+1]) {
            if (strcmp(args[i+1], "pipefail") == 0)
                opt_pipefail = 1;
            else if (strcmp(args[i+1], "noclobber") == 0)
                opt_noclobber = 1;
            else if (strcmp(args[i+1], "errexit") == 0)
                opt_errexit = 1;
            else if (strcmp(args[i+1], "ignoreeof") == 0)
                opt_ignoreeof = 1;
            else if (strcmp(args[i+1], "posix") == 0)
                opt_posix = 1;
            else if (strcmp(args[i+1], "vi") == 0)
                lineedit_mode = LINEEDIT_VI;
            else if (strcmp(args[i+1], "emacs") == 0)
                lineedit_mode = LINEEDIT_EMACS;
            else {
                fprintf(stderr, "set: unknown option %s\n", args[i+1]);
                return 1;
            }
            i++;
        }
        else if (strcmp(args[i], "+e") == 0)
            opt_errexit = 0;
        else if (strcmp(args[i], "+u") == 0)
            opt_nounset = 0;
        else if (strcmp(args[i], "+x") == 0)
            opt_xtrace = 0;
        else if (strcmp(args[i], "+v") == 0)
            opt_verbose = 0;
        else if (strcmp(args[i], "+n") == 0)
            opt_noexec = 0;
        else if (strcmp(args[i], "+f") == 0)
            opt_noglob = 0;
        else if (strcmp(args[i], "+C") == 0)
            opt_noclobber = 0;
        else if (strcmp(args[i], "+a") == 0)
            opt_allexport = 0;
        else if (strcmp(args[i], "+b") == 0)
            opt_notify = 0;
        else if (strcmp(args[i], "+m") == 0)
            opt_monitor = 0;
        else if (strcmp(args[i], "+p") == 0)
            opt_privileged = 0;
        else if (strcmp(args[i], "+t") == 0)
            opt_onecmd = 0;
        else if (strcmp(args[i], "+h") == 0)
            opt_hashall = 0;
        else if (strcmp(args[i], "+k") == 0)
            opt_keyword = 0;
        else if (strcmp(args[i], "+o") == 0 && args[i+1]) {
            if (strcmp(args[i+1], "pipefail") == 0)
                opt_pipefail = 0;
            else if (strcmp(args[i+1], "noclobber") == 0)
                opt_noclobber = 0;
            else if (strcmp(args[i+1], "errexit") == 0)
                opt_errexit = 0;
            else if (strcmp(args[i+1], "ignoreeof") == 0)
                opt_ignoreeof = 0;
            else if (strcmp(args[i+1], "posix") == 0)
                opt_posix = 0;
            else if (strcmp(args[i+1], "vi") == 0)
                lineedit_mode = LINEEDIT_EMACS;
            else if (strcmp(args[i+1], "emacs") == 0)
                lineedit_mode = LINEEDIT_VI;
            else {
                fprintf(stderr, "set: unknown option %s\n", args[i+1]);
                return 1;
            }
            i++;
        }
        else if (args[i][0] == '-' || args[i][0] == '+') {
            fprintf(stderr, "set: unknown option %s\n", args[i]);
            return 1;
        } else {
            break;
        }
    }

    if (args[i]) {
        int count = 0;
        for (int j = i; args[j]; j++)
            count++;

        char *zero = script_argv ? script_argv[0] : NULL;
        char **newv = xcalloc(count + 2, sizeof(char *));
        newv[0] = zero;
        for (int j = 0; j < count; j++) {
            newv[j + 1] = strdup(args[i + j]);
            if (!newv[j + 1]) {
                for (int k = 1; k <= j; k++)
                    free(newv[k]);
                free(newv);
                return 1;
            }
        }
        newv[count + 1] = NULL;

        if (script_argv) {
            for (int j = 1; j <= script_argc; j++)
                free(script_argv[j]);
            free(script_argv);
        }
        script_argv = newv;
        script_argc = count;
        getopts_pos = NULL; /* new $@ invalidates getopts parsing state */
    }
    return 1;
}


/*
 * Evaluate arithmetic expressions supplied to the `let` builtin.  The result
 * is stored in `last_status` as zero/non-zero and the function returns 1.
 */
int builtin_let(char **args) {
    if (!args[1]) {
        last_status = 1;
        return 1;
    }
    char expr[MAX_LINE] = "";
    for (int i = 1; args[i]; i++) {
        if (i > 1 && strlen(expr) < sizeof(expr) - 1)
            strcat(expr, " ");
        strncat(expr, args[i], sizeof(expr) - strlen(expr) - 1);
    }
    int err = 0;
    char *msg = NULL;
    long val = eval_arith(expr, &err, &msg);
    if (err && msg) {
        fprintf(stderr, "arith: %s\n", msg);
        free(msg);
    }
    last_status = err ? 1 : (val != 0 ? 0 : 1);
    return 1;
}

/*
 * Implementation of the `unset` builtin.  Removes shell variables or array
 * elements and unsets environment variables.  Always returns 1 and prints
 * errors when the usage is incorrect.
 */
int builtin_unset(char **args) {
    int remove_func = 0;
    int remove_vars = 0;
    int i = parse_builtin_options(args, "fv", &remove_func, &remove_vars);
    if (i < 0)
        i = 1; /* treat as no options to match previous behavior */
    if (!remove_func && !remove_vars) {
        remove_func = remove_vars = 1;
    }
    if (!args[i]) {
        fprintf(stderr, "usage: unset [-f|-v] NAME...\n");
        return 1;
    }
    for (; args[i]; i++) {
        char *name = args[i];
        if (remove_func) {
            remove_function(name);
            if (!remove_vars)
                continue;
        }
        if (!remove_vars)
            continue;
        char *lb = strchr(name, '[');
        if (lb && name[strlen(name)-1] == ']') {
            char *endptr;
            int idx = strtol(lb+1, &endptr, 10);
            if (*endptr == ']') {
                *lb = '\0';
                int len = 0;
                char **arr = get_shell_array(name, &len);
                if (arr && idx >= 0 && idx < len) {
                    char **tmp = NULL;
                    if (len > 1) {
                        tmp = malloc(sizeof(char*) * (len - 1));
                        if (!tmp) {
                            perror("malloc");
                            *lb = '[';
                            continue;
                        }
                    }
                    int j = 0;
                    for (int k = 0; k < len; k++)
                        if (k != idx)
                            tmp[j++] = arr[k];
                    set_shell_array(name, tmp, len - 1);
                    free(tmp);
                }
                *lb = '[';
            }
        } else {
            unset_var(name);
        }
    }
    return 1;
}

static void list_exports(void)
{
    extern char **environ;
    for (char **e = environ; *e; e++) {
        char *eq = strchr(*e, '=');
        if (eq) {
            printf("export %.*s='", (int)(eq - *e), *e);
            for (const char *p = eq + 1; *p; p++) {
                if (*p == '\'')
                    fputs("'\\''", stdout);
                else
                    fputc(*p, stdout);
            }
            printf("'\n");
        } else {
            printf("export %s\n", *e);
        }
    }
}

/*
 * Export shell variables to the environment.  Arguments may be NAME=value
 * pairs or just variable names.  When only a name is given the variable is
 * marked for export.  If it does not exist it will be created with an empty
 * value.  A usage message is printed on error and the function always returns 1.
 */
int builtin_export(char **args) {
    int status = 0;
    if (!args[1]) {
        fprintf(stderr, "usage: export [-p|-n NAME] NAME[=VALUE]...\n");
        return 1;
    }

    if (strcmp(args[1], "-p") == 0 && !args[2]) {
        list_exports();
        return 1;
    }

    if (strcmp(args[1], "-n") == 0 && args[2] && !args[3]) {
        unset_var(args[2]);
        return 1;
    }

    for (int i = 1; args[i]; i++) {
        char *arg = args[i];
        char *eq = strchr(arg, '=');
        if (eq) {
            *eq = '\0';
            if (export_var(arg, eq + 1) < 0) {
                perror("export");
                status = 1;
            }
            *eq = '=';
        } else {
            const char *val = get_shell_var(arg);
            if (!val) {
                val = "";
                set_shell_var(arg, val);
            }
            if (export_var(arg, val) < 0) {
                perror("export");
                status = 1;
            }
        }
    }
    last_status = status;
    return 1;
}

/* Mark variables as read-only so they cannot be modified. */
int builtin_readonly(char **args) {
    int pflag = 0;
    int i = parse_builtin_options(args, "p", &pflag);
    if (i < 0)
        return fprintf(stderr, "usage: readonly [-p] NAME[=VALUE]...\n"), 1;

    if (pflag && !args[i]) {
        print_readonly_vars();
        return 1;
    }

    if (!args[i]) {
        fprintf(stderr, "usage: readonly [-p] NAME[=VALUE]...\n");
        return 1;
    }

    for (; args[i]; i++) {
        char *arg = args[i];
        char *eq = strchr(arg, '=');
        if (eq) {
            *eq = '\0';
            set_shell_var(arg, eq + 1);
            add_readonly(arg);
            *eq = '=';
        } else {
            if (!get_shell_var(arg) && !get_shell_array(arg, NULL))
                set_shell_var(arg, "");
            add_readonly(arg);
        }
    }
    return 1;
}


/* Declare local variables within a function scope. */
int builtin_local(char **args) {
    if (!args[1])
        return 1;
    for (int i = 1; args[i]; i++) {
        char *arg = args[i];
        char *eq = strchr(arg, '=');
        char *name = eq ? strndup(arg, eq - arg) : strdup(arg);
        if (!name)
            continue;
        record_local_var(name);
        if (eq) {
            char *val = eq + 1;
            size_t vlen = strlen(val);
            if (vlen > 1 && val[0] == '(' && val[vlen - 1] == ')') {
                int count = 0;
                char **vals = parse_array_values(val, &count);
                if (!vals && count > 0) {
                    free(name);
                    continue;
                }
                set_shell_array(name, vals, count);
                for (int j = 0; j < count; j++)
                    free(vals[j]);
                free(vals);
            } else {
                set_shell_var(name, val);
            }
        } else if (!get_shell_var(name) && !get_shell_array(name, NULL)) {
            set_shell_var(name, "");
        }
        free(name);
    }
    return 1;
}



