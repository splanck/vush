/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Command substitution implementation.
 */

#define _GNU_SOURCE
#include "cmd_subst.h"
#include "parser.h" /* for MAX_LINE and parse_line */
#include "execute.h"
#include "options.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>


/* Execute CMD and capture its stdout using the shell itself so that shell
 * variables and functions are visible.  The command's output is returned as a
 * newly allocated string with any trailing newline removed. */
char *command_output(const char *cmd) {
    int saved_notify = opt_notify;
    opt_notify = 0;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        opt_notify = saved_notify;
        return NULL;
    }

    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        char *copy = strdup(cmd);
        Command *c = copy ? parse_line(copy) : NULL;
        free(copy);
        if (c) {
            run_command_list(c, cmd);
            free_commands(c);
        }
        exit(last_status);
    } else if (pid > 0) {
        close(pipefd[1]);
        char out[MAX_LINE];
        size_t total = 0;
        ssize_t n;
        while ((n = read(pipefd[0], out + total, sizeof(out) - 1 - total)) > 0) {
            total += (size_t)n;
            if (total >= sizeof(out) - 1)
                break;
        }
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        opt_notify = saved_notify;
        if (total > 0 && out[total - 1] == '\n')
            total--;
        out[total] = '\0';
        char *ret = strdup(out);
        return ret;
    } else {
        close(pipefd[0]);
        close(pipefd[1]);
        opt_notify = saved_notify;
        return NULL;
    }
}

/* Parse a command substitution starting at *p. Supports both $(...) and
 * backtick forms. On success *p is advanced past the closing delimiter and
 * the command's output is returned. NULL is returned on syntax errors or
 * allocation failures. */
char *parse_substitution(char **p) {
    if (!p || !*p || !**p)
        return NULL;

    int depth = 0;
    int is_dollar = (**p == '$');
    (*p)++;
    if (is_dollar) {
        if (**p != '(')
            return NULL;
        (*p)++; /* skip '(' */
        depth = 1;
    }

    char cmd[MAX_LINE];
    int clen = 0;
    while (**p) {
        if (is_dollar) {
            if (**p == '(') {
                depth++;
            } else if (**p == ')') {
                depth--;
                if (depth == 0) {
                    (*p)++; /* skip closing ')' */
                    break;
                }
            }
        } else if (**p == '`') {
            (*p)++; /* skip closing backtick */
            break;
        }

        if (clen < MAX_LINE - 1)
            cmd[clen++] = **p;
        (*p)++;
    }

    if ((is_dollar && depth > 0) || (!is_dollar && *(*p - 1) != '`')) {
        parse_need_more = 1;
        return NULL;
    }

    cmd[clen] = '\0';
    char *res = command_output(cmd);
    if (!res)
        return NULL;
    return res;
}

