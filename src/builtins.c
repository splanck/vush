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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

extern int last_status;
#include "common.h"
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

struct alias_entry {
    char *name;
    char *value;
    struct alias_entry *next;
};

static struct alias_entry *aliases = NULL;

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

void free_aliases(void) {
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

    const char *dir;
    if (!args[1]) {
        dir = getenv("HOME");
    } else if (strcmp(args[1], "-") == 0) {
        dir = getenv("OLDPWD");
        if (!dir) {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return 1;
        }
        printf("%s\n", dir);
    } else {
        dir = args[1];
    }

    if (chdir(dir) != 0) {
        perror("cd");
    } else {
        char newcwd[PATH_MAX];
        if (getcwd(newcwd, sizeof(newcwd))) {
            const char *pwd = getenv("PWD");
            if (!pwd) pwd = prev;
            setenv("OLDPWD", pwd, 1);
            setenv("PWD", newcwd, 1);
        } else {
            perror("getcwd");
        }
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
        } else {
            fprintf(stderr, "usage: history [-c]\n");
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
    return 1;
}

static int builtin_unalias(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: unalias name\n");
        return 1;
    }
    for (int i = 1; args[i]; i++)
        remove_alias(args[i]);
    return 1;
}

static int builtin_unset(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: unset NAME...\n");
        return 1;
    }
    for (int i = 1; args[i]; i++)
        unsetenv(args[i]);
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
    while (fgets(line, sizeof(line), input)) {
        size_t len = strlen(line);
        if (len && line[len-1] == '\n') line[len-1] = '\0';

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
                run_pipeline(c->pipeline, c->background, line);
            prev = c->op;
        }
        free_commands(cmds);
    }
    fclose(input);
    return 1;
}

static int builtin_help(char **args) {
    (void)args;
    printf("Built-in commands:\n");
    printf("  cd [dir]   Change the current directory ('cd -' toggles)\n");
    printf("  pushd DIR  Push current directory and switch to DIR\n");
    printf("  popd       Switch to directory from stack\n");
    printf("  exit [status]  Exit the shell with optional status\n");
    printf("  pwd        Print the current working directory\n");
    printf("  jobs       List running background jobs\n");
    printf("  fg ID      Wait for job ID in foreground\n");
    printf("  bg ID      Continue job ID in background\n");
    printf("  kill [-SIGNAL] ID   Send a signal to job ID\n");
    printf("  export NAME=value   Set an environment variable\n");
    printf("  unset NAME          Remove an environment variable\n");
    printf("  history [-c]   Show or clear command history\n");
    printf("  alias NAME=VALUE    Set an alias\n");
    printf("  unalias NAME        Remove an alias\n");
    printf("  source FILE (. FILE)   Execute commands from FILE\n");
    printf("  help       Display this help message\n");
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

