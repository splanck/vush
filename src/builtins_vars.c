/* Builtins dealing with variables, aliases and functions */
#define _GNU_SOURCE
#include "builtins.h"
#include "history.h"
#include "parser.h"
#include "execute.h"
#include "scriptargs.h"
#include "options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <linux/limits.h>

extern int last_status;
extern int func_return;

struct var_entry {
    char *name;
    char *value;
    struct var_entry *next;
};

static struct var_entry *shell_vars = NULL;



const char *get_shell_var(const char *name) {
    for (struct var_entry *v = shell_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0)
            return v->value;
    }
    return NULL;
}

void set_shell_var(const char *name, const char *value) {
    for (struct var_entry *v = shell_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            free(v->value);
            v->value = strdup(value);
            return;
        }
    }
    struct var_entry *v = malloc(sizeof(struct var_entry));
    if (!v) { perror("malloc"); return; }
    v->name = strdup(name);
    v->value = strdup(value);
    v->next = shell_vars;
    shell_vars = v;
}

void unset_shell_var(const char *name) {
    struct var_entry *prev = NULL;
    for (struct var_entry *v = shell_vars; v; prev = v, v = v->next) {
        if (strcmp(v->name, name) == 0) {
            if (prev)
                prev->next = v->next;
            else
                shell_vars = v->next;
            free(v->name);
            free(v->value);
            free(v);
            return;
        }
    }
}

void free_shell_vars(void) {
    struct var_entry *v = shell_vars;
    while (v) {
        struct var_entry *n = v->next;
        free(v->name);
        free(v->value);
        free(v);
        v = n;
    }
    shell_vars = NULL;
}

int builtin_shift(char **args) {
    (void)args;
    if (script_argc > 0) {
        free(script_argv[1]);
        for (int i = 2; i <= script_argc; i++)
            script_argv[i - 1] = script_argv[i];
        script_argc--;
        script_argv[script_argc + 1] = NULL;
    }
    return 1;
}

int builtin_set(char **args) {
    for (int i = 1; args[i]; i++) {
        if (strcmp(args[i], "-e") == 0)
            opt_errexit = 1;
        else if (strcmp(args[i], "-u") == 0)
            opt_nounset = 1;
        else if (strcmp(args[i], "-x") == 0)
            opt_xtrace = 1;
        else if (strcmp(args[i], "+e") == 0)
            opt_errexit = 0;
        else if (strcmp(args[i], "+u") == 0)
            opt_nounset = 0;
        else if (strcmp(args[i], "+x") == 0)
            opt_xtrace = 0;
        else {
            fprintf(stderr, "set: unknown option %s\n", args[i]);
            return 1;
        }
    }
    return 1;
}

int builtin_read(char **args) {
    int raw = 0;
    int idx = 1;
    if (args[idx] && strcmp(args[idx], "-r") == 0) {
        raw = 1;
        idx++;
    }
    if (!args[idx]) {
        fprintf(stderr, "usage: read [-r] NAME...\n");
        last_status = 1;
        return 1;
    }

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), stdin)) {
        last_status = 1;
        return 1;
    }

    size_t len = strlen(line);
    if (len && line[len - 1] == '\n')
        line[--len] = '\0';

    if (!raw) {
        char *src = line;
        char *dst = line;
        while (*src) {
            if (*src == '\\' && src[1]) {
                src++;
                *dst++ = *src++;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
    }

    int var_count = 0;
    for (int i = idx; args[i]; i++)
        var_count++;

    char *p = line;
    for (int i = 0; i < var_count; i++) {
        while (*p == ' ' || *p == '\t')
            p++;
        char *val = p;
        if (i < var_count - 1) {
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = '\0';
        } else {
            /* last variable gets the rest of the line */
        }
        set_shell_var(args[idx + i], val);
    }
    last_status = 0;
    return 1;
}

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
    long val = eval_arith(expr);
    last_status = (val != 0) ? 0 : 1;
    return 1;
}

int builtin_unset(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: unset NAME...\n");
        return 1;
    }
    for (int i = 1; args[i]; i++) {
        unsetenv(args[i]);
        unset_shell_var(args[i]);
    }
    return 1;
}

int builtin_export(char **args) {
    if (!args[1] || !strchr(args[1], '=')) {
        fprintf(stderr, "usage: export NAME=value\n");
        return 1;
    }

    char *pair = args[1];
    char *eq = strchr(pair, '=');
    *eq = '\0';
    if (setenv(pair, eq + 1, 1) != 0) {
        perror("export");
    }
    set_shell_var(pair, eq + 1);
    *eq = '=';
    return 1;
}

static char *getopts_pos = NULL;

int builtin_getopts(char **args) {
    if (!args[1] || !args[2]) {
        fprintf(stderr, "usage: getopts optstring var\n");
        last_status = 1;
        return 1;
    }

    const char *optstr = args[1];
    const char *var = args[2];
    int silent = 0;
    if (optstr[0] == ':') {
        silent = 1;
        optstr++;
    }

    const char *ind_s = get_shell_var("OPTIND");
    int ind = ind_s ? atoi(ind_s) : 1;
    if (ind < 1)
        ind = 1;

    if (!script_argv)
        ind = 1;

    if (!script_argv || ind > script_argc) {
        set_shell_var(var, "?");
        set_shell_var("OPTARG", "");
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", ind);
        set_shell_var("OPTIND", buf);
        last_status = 1;
        getopts_pos = NULL;
        return 1;
    }

    if (!getopts_pos || *getopts_pos == '\0') {
        char *arg = script_argv[ind];
        if (strcmp(arg, "--") == 0) {
            ind++;
            set_shell_var(var, "?");
            set_shell_var("OPTARG", "");
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", ind);
            set_shell_var("OPTIND", buf);
            last_status = 1;
            getopts_pos = NULL;
            return 1;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            set_shell_var(var, "?");
            set_shell_var("OPTARG", "");
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", ind);
            set_shell_var("OPTIND", buf);
            last_status = 1;
            getopts_pos = NULL;
            return 1;
        }
        getopts_pos = arg + 1;
        ind++;
    }

    char opt = *getopts_pos++;
    if (!opt || opt == ':') {
        opt = '?';
    }
    const char *p = strchr(optstr, opt);
    if (!p) {
        if (!silent)
            fprintf(stderr, "getopts: illegal option -- %c\n", opt);
        set_shell_var(var, "?");
        if (silent) {
            char ob[2] = {opt, '\0'};
            set_shell_var("OPTARG", ob);
        } else {
            set_shell_var("OPTARG", "");
        }
        if (!getopts_pos || *getopts_pos == '\0')
            getopts_pos = NULL;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", ind);
        set_shell_var("OPTIND", buf);
        last_status = 0;
        return 1;
    }

    if (p[1] == ':') {
        if (*getopts_pos != '\0') {
            set_shell_var("OPTARG", getopts_pos);
            getopts_pos = NULL;
        } else if (ind <= script_argc) {
            set_shell_var("OPTARG", script_argv[ind]);
            ind++;
            getopts_pos = NULL;
        } else {
            if (!silent)
                fprintf(stderr, "getopts: option requires an argument -- %c\n", opt);
            if (silent) {
                char ob[2] = {opt, '\0'};
                set_shell_var(var, ":");
                set_shell_var("OPTARG", ob);
            } else {
                set_shell_var(var, "?");
                set_shell_var("OPTARG", "");
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", ind);
            set_shell_var("OPTIND", buf);
            last_status = 0;
            return 1;
        }
    } else {
        set_shell_var("OPTARG", "");
        if (!getopts_pos || *getopts_pos == '\0')
            getopts_pos = NULL;
    }

    char val[2] = {opt, '\0'};
    set_shell_var(var, val);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", ind);
    set_shell_var("OPTIND", buf);
    last_status = 0;
    return 1;
}


