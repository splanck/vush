/* File system related builtins */
#define _GNU_SOURCE
#include "builtins.h"
#include "dirstack.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

int builtin_cd(char **args) {
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

int builtin_pushd(char **args) {
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

int builtin_popd(char **args) {
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

int builtin_pwd(char **args) {
    (void)args;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
    return 1;
}

