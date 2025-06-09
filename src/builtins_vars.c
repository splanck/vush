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

struct alias_entry {
    char *name;
    char *value;
    struct alias_entry *next;
};

static struct alias_entry *aliases = NULL;

struct var_entry {
    char *name;
    char *value;
    struct var_entry *next;
};

static struct var_entry *shell_vars = NULL;

struct func_entry {
    char *name;
    char *text;
    Command *body;
    struct func_entry *next;
};

static struct func_entry *functions = NULL;

static const char *funcfile_path(void) {
    const char *env = getenv("VUSH_FUNCFILE");
    if (env && *env)
        return env;
    const char *home = getenv("HOME");
    if (!home)
        return NULL;
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.vush_funcs", home);
    return path;
}

static void set_alias(const char *name, const char *value);

static const char *aliasfile_path(void) {
    const char *env = getenv("VUSH_ALIASFILE");
    if (env && *env)
        return env;
    const char *home = getenv("HOME");
    if (!home)
        return NULL;
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.vush_aliases", home);
    return path;
}

static void save_aliases(void) {
    const char *path = aliasfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (struct alias_entry *a = aliases; a; a = a->next)
        fprintf(f, "%s=%s\n", a->name, a->value);
    fclose(f);
}

static void save_functions(void) {
    const char *path = funcfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (struct func_entry *fn = functions; fn; fn = fn->next)
        fprintf(f, "%s() { %s }\n", fn->name, fn->text);
    fclose(f);
}

void load_aliases(void) {
    const char *path = aliasfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        set_alias(line, eq + 1);
    }
    fclose(f);
}

void load_functions(void) {
    const char *path = funcfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        Command *cmds = parse_line(line);
        for (Command *c = cmds; c; c = c->next) {
            if (c->type == CMD_FUNCDEF) {
                define_function(c->var, c->body, c->text);
                c->body = NULL;
            }
        }
        free_commands(cmds);
    }
    fclose(f);
}

const char *get_alias(const char *name) {
    for (struct alias_entry *a = aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0)
            return a->value;
    }
    return NULL;
}

static void set_alias(const char *name, const char *value) {
    for (struct alias_entry *a = aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0) {
            free(a->value);
            a->value = strdup(value);
            return;
        }
    }
    struct alias_entry *new_alias = malloc(sizeof(struct alias_entry));
    if (!new_alias) {
        perror("malloc");
        return;
    }
    new_alias->name = strdup(name);
    new_alias->value = strdup(value);
    new_alias->next = aliases;
    aliases = new_alias;
}

static void remove_alias(const char *name) {
    struct alias_entry *prev = NULL;
    for (struct alias_entry *a = aliases; a; prev = a, a = a->next) {
        if (strcmp(a->name, name) == 0) {
            if (prev)
                prev->next = a->next;
            else
                aliases = a->next;
            free(a->name);
            free(a->value);
            free(a);
            return;
        }
    }
}

static void list_aliases(void) {
    for (struct alias_entry *a = aliases; a; a = a->next)
        printf("%s='%s'\n", a->name, a->value);
}

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

void define_function(const char *name, Command *body, const char *text) {
    for (struct func_entry *f = functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            free(f->name);
            free(f->text);
            free_commands(f->body);
            f->name = strdup(name);
            f->text = strdup(text);
            f->body = body;
            return;
        }
    }
    struct func_entry *fn = malloc(sizeof(struct func_entry));
    if (!fn) {
        perror("malloc");
        return;
    }
    fn->name = strdup(name);
    fn->text = strdup(text);
    fn->body = body;
    fn->next = functions;
    functions = fn;
}

Command *get_function(const char *name) {
    for (struct func_entry *f = functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0)
            return f->body;
    }
    return NULL;
}

static void free_function_entries(void) {
    struct func_entry *f = functions;
    while (f) {
        struct func_entry *next = f->next;
        free(f->name);
        free(f->text);
        free_commands(f->body);
        free(f);
        f = next;
    }
    functions = NULL;
}

void free_functions(void) {
    save_functions();
    free_function_entries();
}

void free_aliases(void) {
    save_aliases();
    struct alias_entry *a = aliases;
    while (a) {
        struct alias_entry *next = a->next;
        free(a->name);
        free(a->value);
        free(a);
        a = next;
    }
    aliases = NULL;
}

int builtin_alias(char **args) {
    if (!args[1]) {
        list_aliases();
        return 1;
    }
    for (int i = 1; args[i]; i++) {
        char *eq = strchr(args[i], '=');
        if (!eq) {
            fprintf(stderr, "usage: alias name=value\n");
            continue;
        }
        *eq = '\0';
        set_alias(args[i], eq + 1);
        *eq = '=';
    }
    save_aliases();
    return 1;
}

int builtin_unalias(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: unalias name\n");
        return 1;
    }
    for (int i = 1; args[i]; i++)
        remove_alias(args[i]);
    save_aliases();
    return 1;
}

int builtin_return(char **args) {
    int status = 0;
    if (args[1])
        status = atoi(args[1]);
    last_status = status;
    func_return = 1;
    return 1;
}

int builtin_shift(char **args) {
    (void)args;
    if (script_argc > 0) {
        for (int i = 1; i < script_argc; i++)
            script_argv[i] = script_argv[i + 1];
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

