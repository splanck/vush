/*
 * Builtins that operate on the file system are implemented here.  This
 * includes commands such as cd, pushd, popd, dirs and pwd that change or
 * display the current working directory.  They are grouped together because
 * they all manipulate the shell's idea of the working directory and rely on
 * the directory stack helpers in dirstack.c.  Each builtin updates PWD and
 * OLDPWD as needed so other parts of the shell and any child processes see
 * the correct directory.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "dirstack.h"
#include "util.h"
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

/* Remove '.' and '..' path segments without resolving symlinks. */
static void canonicalize_logical(const char *path, char *out)
{
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp));
    tmp[PATH_MAX - 1] = '\0';
    char *parts[PATH_MAX / 2];
    int sp = 0;
    const int max_parts = sizeof(parts) / sizeof(parts[0]);
    char *save;
    for (char *t = strtok_r(tmp, "/", &save); t; t = strtok_r(NULL, "/", &save)) {
        if (strcmp(t, ".") == 0) {
            continue;
        } else if (strcmp(t, "..") == 0) {
            if (sp > 0)
                sp--;
        } else {
            if (sp >= max_parts) {
                fprintf(stderr,
                        "cd: path has too many components\n");
                break;
            }
            parts[sp++] = t;
        }
    }
    char *p = out;
    if (path[0] == '/')
        *p++ = '/';
    for (int i = 0; i < sp; i++) {
        size_t len = strlen(parts[i]);
        if (p - out + len + 1 >= PATH_MAX)
            break;
        memcpy(p, parts[i], len);
        p += len;
        if (i < sp - 1)
            *p++ = '/';
    }
    if (p == out)
        *p++ = '/';
    *p = '\0';
}

/*
 * builtin_cd - implement the cd command.  Changes the current directory and
 * sets PWD and OLDPWD.  When CDPATH is used the resolved directory is printed
 * to stdout.
 */
int builtin_cd(char **args) {
    char prev[PATH_MAX];
    if (!getcwd(prev, sizeof(prev))) {
        perror("getcwd");
        return 1;
    }

    int physical = 0;        /* -P option */
    int idx = 1;
    if (args[idx] && args[idx][0] == '-' && args[idx][1] && !args[idx][2]) {
        if (args[idx][1] == 'P') {
            physical = 1;
            idx++;
        } else if (args[idx][1] == 'L') {
            idx++;
        }
    }

    const char *target;
    if (!args[idx]) {
        target = getenv("HOME");
    } else if (strcmp(args[idx], "-") == 0) {
        char buf[PATH_MAX];
        target = getenv("OLDPWD");
        if (!target) {
            if (!getcwd(buf, sizeof(buf))) {
                perror("getcwd");
                return 1;
            }
            target = buf;
        }
        printf("%s\n", target);
    } else {
        target = args[idx];
    }

    char used[PATH_MAX];
    const char *dir = target ? target : "";
    int searched = 0;

    if (args[idx] && strcmp(args[idx], "-") != 0 && dir[0] != '/' && dir[0] != '.' &&
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
        const char *path = dir;
        if (physical) {
            if (!realpath(dir, used)) {
                perror("cd");
                return 1;
            }
            path = used;
        }
        if (chdir(path) != 0) {
            perror("cd");
            return 1;
        }
        if (physical)
            dir = path;
    } else if (physical) {
        if (realpath(".", used))
            dir = used;
    }

    const char *oldpwd = getenv("PWD");
    if (!oldpwd)
        oldpwd = prev;
    setenv("OLDPWD", oldpwd, 1);

    char newpwd[PATH_MAX];
    if (physical) {
        if (!realpath(".", newpwd)) {
            if (!getcwd(newpwd, sizeof(newpwd))) {
                strncpy(newpwd, dir, sizeof(newpwd) - 1);
                newpwd[PATH_MAX - 1] = '\0';
            }
        }
    } else {
        if (dir[0] == '/') {
            strncpy(newpwd, dir, sizeof(newpwd));
            newpwd[PATH_MAX - 1] = '\0';
        } else {
            int n = snprintf(newpwd, sizeof(newpwd), "%s/%s", oldpwd, dir);
            if (n < 0 || n >= (int)sizeof(newpwd)) {
                fprintf(stderr, "cd: path too long\n");
                return 1;
            }
        }
        canonicalize_logical(newpwd, newpwd);
    }
    setenv("PWD", newpwd, 1);

    if (searched) {
        printf("%s\n", dir);
    }

    return 1;
}

/*
 * builtin_pushd - push the current directory onto the directory stack and
 * change to the one supplied.  Updates PWD and OLDPWD and prints the new
 * stack after the change.
 */
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

/*
 * builtin_popd - remove the top directory from the directory stack and change
 * to it.  PWD and OLDPWD are updated and the resulting stack is printed.
 */
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

/*
 * builtin_dirs - display the contents of the directory stack.  This builtin
 * does not modify the environment or current directory; it simply prints the
 * stack managed by dirstack.c.
 */
int builtin_dirs(char **args) {
    if (args[1]) {
        fprintf(stderr, "usage: dirs\n");
        return 1;
    }
    dirstack_print();
    return 1;
}

/*
 * builtin_pwd - print the current working directory.  This command simply
 * queries getcwd and writes the result to stdout; it has no side effects.
 */
int builtin_pwd(char **args) {
    int physical = 0; /* -P option */
    int idx = 1;
    if (args[idx] && args[idx][0] == '-' && args[idx][1] && !args[idx][2]) {
        if (args[idx][1] == 'P') {
            physical = 1;
            idx++;
        } else if (args[idx][1] == 'L') {
            idx++;
        } else {
            fprintf(stderr, "usage: pwd [-L|-P]\n");
            return 1;
        }
    }

    if (args[idx]) {
        fprintf(stderr, "usage: pwd [-L|-P]\n");
        return 1;
    }

    if (physical || !getenv("PWD")) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            printf("%s\n", cwd);
        } else {
            perror("pwd");
        }
    } else {
        printf("%s\n", getenv("PWD"));
    }
    return 1;
}

