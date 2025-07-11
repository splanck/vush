/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Parser utility helpers.
 */

/*
 * Parsing utility helpers extracted from parser.c
 */
#define _GNU_SOURCE
#include "parser.h"
#include "lexer.h"
#include "execute.h"
#include "shell_state.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include "util.h"


/* Temporary variable tracking for process substitutions */
struct proc_sub {
    char *path;
    pid_t pid;
    struct proc_sub *next;
};
static struct proc_sub *proc_subs = NULL;

/* Track a new process substitution FIFO. */
static int add_proc_sub(const char *path, pid_t pid) {
    struct proc_sub *ps = malloc(sizeof(struct proc_sub));
    if (!ps)
        return 0;
    ps->path = strdup(path);
    if (!ps->path) {
        free(ps);
        return 0;
    }
    ps->pid = pid;
    ps->next = proc_subs;
    proc_subs = ps;
    return 1;
}

/* Remove a tracked process substitution FIFO. */
static void remove_proc_sub(const char *path) {
    struct proc_sub **pp = &proc_subs;
    while (*pp) {
        if (strcmp((*pp)->path, path) == 0) {
            struct proc_sub *ps = *pp;
            *pp = ps->next;
            if (ps->pid > 0) {
                kill(ps->pid, SIGTERM);
                waitpid(ps->pid, NULL, 0);
            }
            if (ps->path)
                unlink(ps->path);
            free(ps->path);
            free(ps);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* Wait for and remove any remaining process substitutions. */
void cleanup_proc_subs(void) {
    struct proc_sub *ps = proc_subs;
    while (ps) {
        struct proc_sub *n = ps->next;
        if (ps->pid > 0)
            waitpid(ps->pid, NULL, 0);
        if (ps->path)
            unlink(ps->path);
        free(ps->path);
        free(ps);
        ps = n;
    }
    proc_subs = NULL;
}

/* Collect tokens until one of STOPS is encountered. */
char *gather_until(char **p, const char **stops, int nstops, int *idx) {
    char *res = NULL;
    if (idx) *idx = -1;
    while (**p) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '\0') break;
        int quoted = 0; int do_expand = 1;
        char *tok = read_token(p, &quoted, &do_expand);
        if (!tok) {
            free(res); return NULL;
        }
        if (!quoted) {
            for (int i = 0; i < nstops; i++) {
                if (strcmp(tok, stops[i]) == 0) {
                    if (idx) *idx = i;
                    free(tok);
                    return res ? res : strdup("");
                }
            }
        }
        if (res) {
            char *tmp;
            int ret = xasprintf(&tmp, "%s %s", res, tok);
            if (ret == -1 || tmp == NULL) {
                free(res);
                free(tok);
                res = NULL;
                return NULL;
            }
            free(res);
            res = tmp;
        } else {
            res = strdup(tok);
            if (!res) {
                free(tok);
                free(res);
                return NULL;
            }
        }
        free(tok);
    }
    return res ? res : strdup("");
}

/* Gather tokens until an unquoted 'done' is found while tracking nested
 * 'do'/'done' pairs.  This allows nested loop bodies to be parsed correctly. */
char *gather_until_done(char **p) {
    char *res = NULL;
    int depth = 0;
    while (**p) {
        while (**p == ' ' || **p == '\t')
            (*p)++;
        if (**p == '\0')
            break;
        int quoted = 0;
        int do_expand = 1;
        char *tok = read_token(p, &quoted, &do_expand);
        if (!tok) {
            free(res);
            return NULL;
        }
        if (!quoted) {
            if (strcmp(tok, "do") == 0) {
                depth++;
            } else if (strcmp(tok, "done") == 0) {
                if (depth == 0) {
                    free(tok);
                    return res ? res : strdup("");
                }
                depth--;
            }
        }
        if (res) {
            char *tmp;
            if (xasprintf(&tmp, "%s %s", res, tok) == -1 || !tmp) {
                free(res);
                free(tok);
                return NULL;
            }
            free(res);
            res = tmp;
        } else {
            res = strdup(tok);
            if (!res) {
                free(tok);
                return NULL;
            }
        }
        free(tok);
    }
    return res ? res : strdup("");
}

/* Return text inside matching braces starting at *p. */
char *gather_braced(char **p) {
    if (**p != '{')
        return NULL;
    (*p)++; /* skip '{' */
    char *start = *p;
    int depth = 1;
    int in_s = 0, in_d = 0, esc = 0;
    while (**p) {
        char c = **p;
        if (esc) {
            esc = 0;
        } else if (c == '\\') {
            esc = 1;
        } else if (c == '\'' && !in_d) {
            in_s = !in_s;
        } else if (c == '"' && !in_s) {
            in_d = !in_d;
        } else if (!in_s && !in_d) {
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) {
                    size_t len = (size_t)(*p - start);
                    char *res = strndup(start, len);
                    (*p)++; /* skip closing brace */
                    return res;
                }
            }
        }
        (*p)++;
    }
    return NULL;
}

/* Return text inside matching parentheses starting at *p. */
char *gather_parens(char **p) {
    if (**p != '(')
        return NULL;
    (*p)++; /* skip '(' */
    char *start = *p;
    int depth = 1;
    int in_s = 0, in_d = 0, esc = 0;
    while (**p) {
        char c = **p;
        if (esc) {
            esc = 0;
        } else if (c == '\\') {
            esc = 1;
        } else if (c == '\'' && !in_d) {
            in_s = !in_s;
        } else if (c == '"' && !in_s) {
            in_d = !in_d;
        } else if (!in_s && !in_d) {
            if (c == '(') depth++;
            else if (c == ')') {
                depth--;
                if (depth == 0) {
                    size_t len = (size_t)(*p - start);
                    char *res = strndup(start, len);
                    (*p)++; /* skip closing paren */
                    return res;
                }
            }
        }
        (*p)++;
    }
    return NULL;
}

/* Duplicate S without leading or trailing whitespace. */
char *trim_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    const char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end-1))) end--;
    return strndup(s, end - s);
}

/* Extract contents of a double parenthesis arithmetic expression. */
char *gather_dbl_parens(char **p) {
    if (strncmp(*p, "((", 2) != 0)
        return NULL;
    *p += 2;               /* skip initial '((' */
    char *start = *p;
    int depth = 0;
    int in_s = 0, in_d = 0, esc = 0;
    while (**p) {
        char c = **p;
        if (esc) {
            esc = 0;
        } else if (c == '\\') {
            esc = 1;
        } else if (c == '\'' && !in_d) {
            in_s = !in_s;
        } else if (c == '"' && !in_s) {
            in_d = !in_d;
        } else if (!in_s && !in_d) {
            if (c == '(') {
                depth++;
            } else if (c == ')') {
                if (depth == 0 && *(*p + 1) == ')') {
                    char *res = strndup(start, *p - start);
                    *p += 2;
                    return res;
                }
                if (depth > 0)
                    depth--;
            }
        }
        (*p)++;
    }
    return NULL;
}

/* Parse a <( ) or >( ) process substitution and return the FIFO path. */
char *process_substitution(char **p, int read_from) {
    char *body = gather_parens(p);
    if (!body)
        return NULL;
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    size_t len = strlen(tmpdir) + sizeof("/vushpsXXXXXX");
    char *template = malloc(len);
    if (!template) {
        perror("malloc");
        free(body);
        return NULL;
    }
    snprintf(template, len, "%s/vushpsXXXXXX", tmpdir);
    int fd = mkstemp(template);
    if (fd < 0) {
        perror("mkstemp");
        free(body);
        return NULL;
    }
    close(fd);
    unlink(template);
    if (mkfifo(template, 0600) != 0) {
        perror("mkfifo");
        free(template);
        free(body);
        return NULL;
    }
    char *copy = strdup(body);
    Command *cmd = NULL;
    if (copy)
        cmd = parse_line(copy);
    free(copy);
    if (!cmd) {
        unlink(template);
        free(body);
        return NULL;
    }
    pid_t pid = fork();
    int added = 0;
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        int f = open(template, read_from ? O_RDONLY : O_WRONLY);
        if (f < 0) { perror(template); exit(1); }
        if (read_from)
            dup2(f, STDIN_FILENO);
        else
            dup2(f, STDOUT_FILENO);
        close(f);
        run_command_list(cmd, body);
        exit(last_status);
    } else if (pid > 0) {
        if (!add_proc_sub(template, pid)) {
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            unlink(template);
            free_commands(cmd);
            free(body);
            free(template);
            return NULL;
        }
        added = 1;
    } else {
        perror("fork");
        unlink(template);
        free_commands(cmd);
        free(body);
        free(template);
        return NULL;
    }
    free_commands(cmd);
    free(body);
    char *res = strdup(template);
    if (!res) {
        if (added)
            remove_proc_sub(template);
        else
            unlink(template);
        free(template);
        return NULL;
    }
    free(template);
    return res;
}

