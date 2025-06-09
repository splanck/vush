/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#define _GNU_SOURCE
#include "builtins.h"
#include "jobs.h"
#include "history.h"
#include "parser.h"
#include "execute.h"
#include "dirstack.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

extern int last_status;
extern FILE *parse_input;
extern int func_return;
#include "common.h"
#include "scriptargs.h"
#include "options.h"
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

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


static int builtin_cd(char **args) {
    char prev[PATH_MAX];
    if (!getcwd(prev, sizeof(prev))) {
        perror("getcwd");
        return 1;
    }

    const char *target;
    if (!args[1]) {
        target = getenv("HOME");
    } else if (strcmp(args[1], "-") == 0) {
        target = getenv("OLDPWD");
        if (!target) {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return 1;
        }
        printf("%s\n", target);
    } else {
        target = args[1];
    }

    char used[PATH_MAX];
    const char *dir = target ? target : "";
    int searched = 0;

    if (args[1] && strcmp(args[1], "-") != 0 && dir[0] != '/' && dir[0] != '.' &&
        strchr(dir, '/') == NULL) {
        const char *cdpath = getenv("CDPATH");
        if (cdpath && *cdpath) {
            char *paths = strdup(cdpath);
            if (paths) {
                for (char *p = strtok(paths, ":"); p; p = strtok(NULL, ":")) {
                    const char *base = *p ? p : ".";
                    snprintf(used, sizeof(used), "%s/%s", base, dir);
                    if (chdir(used) == 0) {
                        dir = used;
                        searched = 1;
                        break;
                    }
                }
                free(paths);
            }
        }
    }

    if (!searched) {
        if (chdir(dir) != 0) {
            perror("cd");
            return 1;
        }
    }

    char newcwd[PATH_MAX];
    if (getcwd(newcwd, sizeof(newcwd))) {
        const char *pwd = getenv("PWD");
        if (!pwd) pwd = prev;
        setenv("OLDPWD", pwd, 1);
        setenv("PWD", newcwd, 1);
    } else {
        perror("getcwd");
    }

    if (args[1] && strcmp(args[1], "-") != 0 && strcmp(dir, args[1]) != 0) {
        printf("%s\n", dir);
    }

    return 1;
}

static int builtin_pushd(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: pushd dir\n");
        return 1;
    }
    char prev[PATH_MAX];
    if (!getcwd(prev, sizeof(prev))) {
        perror("getcwd");
        return 1;
    }
    if (chdir(args[1]) != 0) {
        perror("pushd");
        return 1;
    }
    dirstack_push(prev);
    char newcwd[PATH_MAX];
    if (getcwd(newcwd, sizeof(newcwd))) {
        const char *pwd = getenv("PWD");
        if (!pwd) pwd = prev;
        setenv("OLDPWD", pwd, 1);
        setenv("PWD", newcwd, 1);
    }
    dirstack_print();
    return 1;
}

static int builtin_popd(char **args) {
    (void)args;
    char *dir = dirstack_pop();
    if (!dir) {
        fprintf(stderr, "popd: directory stack empty\n");
        return 1;
    }
    char prev[PATH_MAX];
    if (!getcwd(prev, sizeof(prev))) {
        perror("getcwd");
        free(dir);
        return 1;
    }
    if (chdir(dir) != 0) {
        perror("popd");
        free(dir);
        return 1;
    }
    char newcwd[PATH_MAX];
    if (getcwd(newcwd, sizeof(newcwd))) {
        setenv("OLDPWD", prev, 1);
        setenv("PWD", newcwd, 1);
    }
    free(dir);
    dirstack_print();
    return 1;
}

int builtin_dirs(char **args) {
    if (args[1]) {
        fprintf(stderr, "usage: dirs\n");
        return 1;
    }
    dirstack_print();
    return 1;
}

static int builtin_exit(char **args) {
    int status = 0;
    if (args[1]) {
        char *end;
        errno = 0;
        long val = strtol(args[1], &end, 10);
        if (*end != '\0' || errno != 0) {
            fprintf(stderr, "usage: exit [STATUS]\n");
            return 1;
        }
        status = (int)val;
    }
    free_aliases();
    free_functions();
    free_shell_vars();
    exit(status);
}

static int builtin_pwd(char **args) {
    (void)args;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
    return 1;
}

static int builtin_jobs(char **args) {
    (void)args;
    print_jobs();
    return 1;
}

static int builtin_fg(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: fg ID\n");
        return 1;
    }
    int id = atoi(args[1]);
    wait_job(id);
    return 1;
}

static int builtin_bg(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: bg ID\n");
        return 1;
    }
    int id = atoi(args[1]);
    bg_job(id);
    return 1;
}

static int builtin_kill(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: kill [-SIGNAL] ID\n");
        return 1;
    }
    int sig = SIGTERM;
    int idx = 1;
    if (args[1][0] == '-') {
        sig = atoi(args[1] + 1);
        idx++;
    }
    if (!args[idx]) {
        fprintf(stderr, "usage: kill [-SIGNAL] ID\n");
        return 1;
    }
    int id = atoi(args[idx]);
    kill_job(id, sig);
    return 1;
}

static int builtin_history(char **args) {
    if (args[1]) {
        if (strcmp(args[1], "-c") == 0 && !args[2]) {
            clear_history();
            return 1;
        } else if (strcmp(args[1], "-d") == 0 && args[2] && !args[3]) {
            int id = atoi(args[2]);
            delete_history_entry(id);
            return 1;
        } else {
            fprintf(stderr, "usage: history [-c|-d NUMBER]\n");
            return 1;
        }
    }
    print_history();
    return 1;
}

static int builtin_alias(char **args) {
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

static int builtin_unalias(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: unalias name\n");
        return 1;
    }
    for (int i = 1; args[i]; i++)
        remove_alias(args[i]);
    save_aliases();
    return 1;
}

static int builtin_return(char **args) {
    int status = 0;
    if (args[1])
        status = atoi(args[1]);
    last_status = status;
    func_return = 1;
    return 1;
}

static int builtin_shift(char **args) {
    (void)args;
    if (script_argc > 0) {
        for (int i = 1; i < script_argc; i++)
            script_argv[i] = script_argv[i + 1];
        script_argc--;
        script_argv[script_argc + 1] = NULL;
    }
    return 1;
}

static int builtin_set(char **args) {
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

static int builtin_let(char **args) {
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

static int builtin_unset(char **args) {
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

static int builtin_export(char **args) {
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

static int builtin_source(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: source file\n");
        return 1;
    }

    FILE *input = fopen(args[1], "r");
    if (!input) {
        perror(args[1]);
        return 1;
    }

    char line[MAX_LINE];
    while (read_logical_line(input, line, sizeof(line))) {

        parse_input = input;
        Command *cmds = parse_line(line);
        if (!cmds || !cmds->pipeline || !cmds->pipeline->argv[0]) {
            free_commands(cmds);
            continue;
        }

        add_history(line);

        CmdOp prev = OP_SEMI;
        for (Command *c = cmds; c; c = c->next) {
            int run = 1;
            if (c != cmds) {
                if (prev == OP_AND)
                    run = (last_status == 0);
                else if (prev == OP_OR)
                    run = (last_status != 0);
            }
            if (run)
                run_pipeline(c, line);
            prev = c->op;
        }
        free_commands(cmds);
    }
    fclose(input);
    parse_input = stdin;
    return 1;
}

static int builtin_help(char **args) {
    (void)args;
    printf("Built-in commands:\n");
    printf("  cd [dir]   Change the current directory ('cd -' toggles)\n");
    printf("  pushd DIR  Push current directory and switch to DIR\n");
    printf("  popd       Switch to directory from stack\n");
    printf("  dirs       Display the directory stack\n");
    printf("  exit [status]  Exit the shell with optional status\n");
    printf("  pwd        Print the current working directory\n");
    printf("  jobs       List running background jobs\n");
    printf("  fg ID      Wait for job ID in foreground\n");
    printf("  bg ID      Continue job ID in background\n");
    printf("  kill [-SIGNAL] ID   Send a signal to job ID\n");
    printf("  export NAME=value   Set an environment variable\n");
    printf("  unset NAME          Remove an environment variable\n");
    printf("  history [-c|-d NUM]   Show or modify command history\n");
    printf("  alias NAME=VALUE    Set an alias\n");
    printf("  unalias NAME        Remove an alias\n");
    printf("  read [-r] VAR...    Read a line into variables\n");
    printf("  return [status]     Return from a function\n");
    printf("  shift      Shift positional parameters\n");
    printf("  let EXPR  Evaluate arithmetic expression\n");
    printf("  set [-e|-u|-x] Toggle shell options\n");
    printf("  source FILE (. FILE)   Execute commands from FILE\n");
    printf("  help       Display this help message\n");
    return 1;
}

static int builtin_test(char **args) {
    int count = 0;
    while (args[count]) count++;
    if (strcmp(args[0], "[") == 0) {
        if (count < 2 || strcmp(args[count-1], "]") != 0) {
            fprintf(stderr, "[: missing ]\n");
            last_status = 1;
            return 1;
        }
        args[count-1] = NULL;
        count--;
    }
    char **av = args + 1;
    count--;
    int res = 1;
    if (count == 0) {
        res = 1;
    } else if (count == 1) {
        res = av[0][0] ? 0 : 1;
    } else if (count == 2) {
        if (strcmp(av[0], "-n") == 0)
            res = av[1][0] ? 0 : 1;
        else if (strcmp(av[0], "-z") == 0)
            res = av[1][0] ? 1 : 0;
    } else if (count == 3) {
        if (strcmp(av[1], "=") == 0)
            res = strcmp(av[0], av[2]) == 0 ? 0 : 1;
        else if (strcmp(av[1], "!=") == 0)
            res = strcmp(av[0], av[2]) != 0 ? 0 : 1;
        else if (strcmp(av[1], "-eq") == 0)
            res = (atoi(av[0]) == atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-ne") == 0)
            res = (atoi(av[0]) != atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-gt") == 0)
            res = (atoi(av[0]) > atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-lt") == 0)
            res = (atoi(av[0]) < atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-ge") == 0)
            res = (atoi(av[0]) >= atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-le") == 0)
            res = (atoi(av[0]) <= atoi(av[2])) ? 0 : 1;
    }
    last_status = res;
    return 1;
}

struct builtin {
    const char *name;
    int (*func)(char **);
};

static struct builtin builtins[] = {
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
    {"let", builtin_let},
    {"set", builtin_set},
    {"test", builtin_test},
    {"[", builtin_test},
    {"type", builtin_type},
    {"source", builtin_source},
    {".", builtin_source},
    {"help", builtin_help},
    {NULL, NULL}
};

int run_builtin(char **args) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(args[0], builtins[i].name) == 0) {
            return builtins[i].func(args);
        }
    }
    return 0;
}

const char **get_builtin_names(void) {
    static const char *names[sizeof(builtins) / sizeof(builtins[0])];
    int i = 0;
    for (; builtins[i].name; i++)
        names[i] = builtins[i].name;
    names[i] = NULL;
    return names;
}

int builtin_type(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: type name...\n");
        return 1;
    }
    for (int i = 1; args[i]; i++) {
        const char *alias = get_alias(args[i]);
        if (alias) {
            printf("%s is an alias for '%s'\n", args[i], alias);
            continue;
        }
        Command *fn = get_function(args[i]);
        if (fn) {
            printf("%s is a function\n", args[i]);
            continue;
        }
        int is_builtin = 0;
        for (int j = 0; builtins[j].name; j++) {
            if (strcmp(args[i], builtins[j].name) == 0) {
                printf("%s is a builtin\n", args[i]);
                is_builtin = 1;
                break;
            }
        }
        if (is_builtin)
            continue;
        const char *pathenv = getenv("PATH");
        if (!pathenv)
            pathenv = "/bin:/usr/bin";
        char *paths = strdup(pathenv);
        if (!paths)
            continue;
        char *saveptr = NULL;
        char *dir = strtok_r(paths, ":", &saveptr);
        int found = 0;
        while (dir) {
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", dir, args[i]);
            if (access(full, X_OK) == 0) {
                printf("%s is %s\n", args[i], full);
                found = 1;
                break;
            }
            dir = strtok_r(NULL, ":", &saveptr);
        }
        free(paths);
        if (!found)
            printf("%s not found\n", args[i]);
    }
    return 1;
}

