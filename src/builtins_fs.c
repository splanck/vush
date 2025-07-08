/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Directory and path related builtins.
 */

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
#include "shell_state.h"
#include "util.h"
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

/* Remove '.' and '..' path segments without resolving symlinks. */
static void canonicalize_logical(const char *path, char *out, size_t out_size)
{
    char *tmp = strdup(path);
    if (!tmp)
        return;
    size_t max_parts = strlen(tmp) / 2 + 2;
    char **parts = malloc(max_parts * sizeof(char *));
    if (!parts) {
        free(tmp);
        return;
    }
    int sp = 0;
    char *save;
    for (char *t = strtok_r(tmp, "/", &save); t; t = strtok_r(NULL, "/", &save)) {
        if (strcmp(t, ".") == 0) {
            continue;
        } else if (strcmp(t, "..") == 0) {
            if (sp > 0)
                sp--;
        } else {
            if ((size_t)sp >= max_parts) {
                fprintf(stderr,
                        "cd: path has too many components\n");
                last_status = 1;
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
        if ((size_t)(p - out) + len + 1 >= out_size)
            break;
        memcpy(p, parts[i], len);
        p += len;
        if (i < sp - 1)
            *p++ = '/';
    }
    if (p == out)
        *p++ = '/';
    *p = '\0';
    free(parts);
    free(tmp);
}

/* Change to DIR resolving CDPATH and -P when requested. The final directory
 * path is returned, using BUF when needed.  On failure NULL is returned and the
 * current directory is unchanged.  SEARCHED is set when CDPATH was used. */
static const char *resolve_cd_target(const char *dir, int physical, char *buf,
                                     size_t buf_size, int *searched)
{
    const char *res = dir ? dir : "";
    if (searched)
        *searched = 0;

    /* Search CDPATH when the argument does not contain a slash and is not
     * absolute. */
    if (res[0] != '/' && res[0] != '.' && strchr(res, '/') == NULL) {
        const char *cdpath = getenv("CDPATH");
        if (cdpath && *cdpath) {
            char *paths = strdup(cdpath);
            if (paths) {
                for (char *p = strtok(paths, ":"); p; p = strtok(NULL, ":")) {
                    const char *base = *p ? p : ".";
                    int n = snprintf(buf, buf_size, "%s/%s", base, res);
                    if (n < 0 || (size_t)n >= buf_size) {
                        fprintf(stderr, "cd: path too long\n");
                        continue;
                    }
                    if (chdir(buf) == 0) {
                        if (searched)
                            *searched = 1;
                        res = buf;
                        break;
                    }
                }
                free(paths);
            }
        }
    }

    if (res != buf) {
        const char *path = res;
        int unresolved = 0;
        if (physical) {
            if (realpath(res, buf)) {
                path = buf;
            } else {
                unresolved = 1;
            }
        }
        if (chdir(path) != 0)
            return NULL;
        if (physical) {
            if (!unresolved) {
                res = path;
            } else if (getcwd(buf, buf_size)) {
                res = buf;
            }
        }
    } else if (physical) {
        if (realpath(".", buf))
            res = buf;
        else if (getcwd(buf, buf_size))
            res = buf;
    }

    return res;
}

/* Update PWD and OLDPWD after a directory change.  OLDPWD is set to OLDPWD and
 * PWD is computed from DIR according to PHYSICAL. */
static void update_pwd(const char *oldpwd, const char *dir, int physical,
                       size_t pathmax)
{
    if (!oldpwd)
        oldpwd = "";

    char *newpwd = malloc(pathmax);
    if (!newpwd) {
        perror("malloc");
        return;
    }

    if (physical) {
        if (!realpath(".", newpwd)) {
            if (!getcwd(newpwd, pathmax)) {
                strncpy(newpwd, dir, pathmax - 1);
                newpwd[pathmax - 1] = '\0';
            }
        }
    } else {
        if (dir[0] == '/') {
            strncpy(newpwd, dir, pathmax);
            newpwd[pathmax - 1] = '\0';
        } else {
            int n = snprintf(newpwd, pathmax, "%s/%s", oldpwd, dir);
            if (n < 0 || (size_t)n >= pathmax) {
                fprintf(stderr, "cd: path too long\n");
                free(newpwd);
                return;
            }
        }
        canonicalize_logical(newpwd, newpwd, pathmax);
    }

    setenv("OLDPWD", oldpwd, 1);
    setenv("PWD", newpwd, 1);
    free(newpwd);
}

/* Print the current directory using -P or -L semantics. */
static void print_pwd(int physical)
{
    if (physical || !getenv("PWD")) {
        char *cwd = getcwd(NULL, 0);
        if (cwd) {
            printf("%s\n", cwd);
            free(cwd);
        } else {
            perror("pwd");
        }
    } else {
        printf("%s\n", getenv("PWD"));
    }
}

/*
 * builtin_cd - implement the cd command.  Changes the current directory and
 * sets PWD and OLDPWD.  When CDPATH is used the resolved directory is printed
 * to stdout.
 */
int builtin_cd(char **args) {
    size_t pathmax = get_path_max();
    char *prev = getcwd(NULL, 0);
    char *buf = NULL;
    char *used = NULL;
    if (!prev) {
        perror("getcwd");
        last_status = 1;
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
        target = getenv("OLDPWD");
        if (!target) {
            buf = getcwd(NULL, 0);
            if (!buf) {
                perror("getcwd");
                free(prev);
                last_status = 1;
                return 1;
            }
            target = buf;
        }
        printf("%s\n", target);
    } else {
        target = args[idx];
    }

    used = malloc(pathmax);
    if (!used) {
        perror("malloc");
        free(prev);
        free(buf);
        last_status = 1;
        return 1;
    }

    int searched = 0;
    const char *dir = resolve_cd_target(target, physical, used, pathmax, &searched);
    if (!dir) {
        perror("cd");
        free(prev);
        free(buf);
        free(used);
        last_status = 1;
        return 1;
    }

    const char *oldpwd = getenv("PWD");
    if (!oldpwd)
        oldpwd = prev;
    update_pwd(oldpwd, dir, physical, pathmax);

    if (searched)
        printf("%s\n", dir);

    free(prev);
    free(buf);
    free(used);
    last_status = 0;
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
    size_t pathmax = get_path_max();
    char *prev = getcwd(NULL, 0);
    if (!prev) {
        perror("getcwd");
        return 1;
    }
    if (chdir(args[1]) != 0) {
        perror("pushd");
        free(prev);
        return 1;
    }
    dirstack_push(prev);
    update_pwd(prev, args[1], 1, pathmax);
    free(prev);
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
    size_t pathmax = get_path_max();
    char *prev = getcwd(NULL, 0);
    if (!prev) {
        perror("getcwd");
        free(dir);
        return 1;
    }
    if (chdir(dir) != 0) {
        perror("popd");
        free(dir);
        free(prev);
        return 1;
    }
    update_pwd(prev, dir, 1, pathmax);
    free(prev);
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

    print_pwd(physical);
    return 1;
}

