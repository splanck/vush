/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#define _GNU_SOURCE
#include "builtins.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int builtin_cd(char **args) {
    const char *dir = args[1] ? args[1] : getenv("HOME");
    if (chdir(dir) != 0) {
        perror("cd");
    }
    return 1;
}

static int builtin_exit(char **args) {
    (void)args;
    exit(0);
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

static int builtin_help(char **args) {
    (void)args;
    printf("Built-in commands:\n");
    printf("  cd [dir]   Change the current directory\n");
    printf("  exit       Exit the shell\n");
    printf("  pwd        Print the current working directory\n");
    printf("  jobs       List running background jobs\n");
    printf("  export NAME=value   Set an environment variable\n");
    printf("  help       Display this help message\n");
    return 1;
}

struct builtin {
    const char *name;
    int (*func)(char **);
};

static struct builtin builtins[] = {
    {"cd", builtin_cd},
    {"exit", builtin_exit},
    {"pwd", builtin_pwd},
    {"jobs", builtin_jobs},
    {"export", builtin_export},
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

