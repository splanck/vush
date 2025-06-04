/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#define _GNU_SOURCE
#include "parser.h"
#include "builtins.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glob.h>
#include <pwd.h>
#include <ctype.h>
#include <ctype.h>

extern int last_status;

char *expand_var(const char *token) {
    if (strcmp(token, "$?") == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", last_status);
        return strdup(buf);
    }
    if (token[0] == '~') {
        const char *rest = token + 1;
        const char *home = NULL;
        if (*rest == '/' || *rest == '\0') {
            home = getenv("HOME");
        } else {
            const char *slash = strchr(rest, '/');
            size_t len = slash ? (size_t)(slash - rest) : strlen(rest);
            char *user = strndup(rest, len);
            if (user) {
                struct passwd *pw = getpwnam(user);
                if (pw) home = pw->pw_dir;
                free(user);
            }
            rest = slash ? slash : rest + len;
        }
        if (!home) home = getenv("HOME");
        if (!home) home = "";
        char *ret = malloc(strlen(home) + strlen(rest) + 1);
        if (!ret) return NULL;
        strcpy(ret, home);
        strcat(ret, rest);
        return ret;
    }
    if (token[0] != '$') return strdup(token);
    const char *val = getenv(token + 1);
    return strdup(val ? val : "");
}

static char *command_output(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("");
    char out[MAX_LINE];
    size_t total = 0;
    while (fgets(out + total, sizeof(out) - total, fp)) {
        total = strlen(out);
        if (total >= sizeof(out) - 1) break;
    }
    pclose(fp);
    if (total > 0 && out[total - 1] == '\n')
        out[total - 1] = '\0';
    return strdup(out);
}

static char *read_token(char **p, int *quoted) {
    char buf[MAX_LINE];
    int len = 0;
    int do_expand = 1;
    *quoted = 0;

    if (**p == '>' || **p == '<' || **p == '|' || **p == '&' || **p == ';') {
        buf[len++] = **p;
        (*p)++;
        if ((buf[0] == '>' && **p == '>') ||
            (buf[0] == '&' && **p == '&') ||
            (buf[0] == '|' && **p == '|')) {
            buf[len++] = **p;
            (*p)++;
        }
        buf[len] = '\0';
        return strdup(buf);
    }

    if (**p == '\'') {
        *quoted = 1;
        do_expand = 0;
        (*p)++; /* skip opening quote */
        while (**p && **p != '\'' && len < MAX_LINE - 1) {
            buf[len++] = *(*p)++;
        }
        if (**p == '\'') (*p)++; /* skip closing quote */
    } else {
        int in_double = 0;
        if (**p == '"') {
            in_double = 1;
            *quoted = 1;
            (*p)++; /* skip opening quote */
        }
        int first = 1;
        while (**p && (in_double || (**p != ' ' && **p != '\t' && **p != '|' &&
                **p != '<' && **p != '>' && **p != '&' && **p != ';'))) {
            if (**p == '`' || (**p == '$' && *(*p + 1) == '(')) {
                int depth = 0;
                int is_dollar = (**p == '$');
                (*p)++; /* skip ` or $ */
                if (is_dollar) {
                    (*p)++; /* skip '(' */
                    depth = 1;
                }
                char cmd[MAX_LINE];
                int clen = 0;
                while (**p && ((is_dollar && depth > 0) || (!is_dollar && **p != '`')) && clen < MAX_LINE - 1) {
                    if (is_dollar) {
                        if (**p == '(') depth++;
                        else if (**p == ')') {
                            depth--;
                            if (depth == 0) { (*p)++; break; }
                        }
                    }
                    if (!is_dollar && **p == '`') break;
                    cmd[clen++] = **p;
                    (*p)++;
                }
                if (!is_dollar && **p == '`') (*p)++; /* closing backtick */
                cmd[clen] = '\0';
                char *res = command_output(cmd);
                for (int ci = 0; res[ci] && len < MAX_LINE - 1; ci++)
                    buf[len++] = res[ci];
                free(res);
                first = 0;
                continue;
            }
            if (**p == '\\') {
                (*p)++;
                if (**p) {
                    if (len < MAX_LINE - 1) buf[len++] = **p;
                    if (first) do_expand = 0;
                    if (**p) (*p)++;
                }
                first = 0;
                continue;
            }
            if (in_double && **p == '"') {
                (*p)++; /* end quote */
                break;
            }
            if (len < MAX_LINE - 1) buf[len++] = **p;
            (*p)++;
            first = 0;
        }
    }

    buf[len] = '\0';
    return do_expand ? expand_var(buf) : strdup(buf);
}

Command *parse_line(char *line) {
    char *p = line;
    Command *head = NULL, *cur_cmd = NULL;

    while (1) {
        PipelineSegment *seg_head = calloc(1, sizeof(PipelineSegment));
        PipelineSegment *seg = seg_head;
        int argc = 0;
        int background = 0;
        CmdOp op = OP_NONE;

        while (*p && argc < MAX_TOKENS - 1) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0' || *p == '#') { op = OP_NONE; break; }

            if (*p == ';') { op = OP_SEMI; p++; break; }
            if (*p == '&' && *(p+1) == '&') { op = OP_AND; p += 2; break; }
            if (*p == '|' && *(p+1) == '|') { op = OP_OR; p += 2; break; }

            if (*p == '|') {
                seg->argv[argc] = NULL;
                PipelineSegment *next = calloc(1, sizeof(PipelineSegment));
                seg->next = next;
                seg = next;
                argc = 0;
                p++;
                continue;
            }

            int quoted = 0;
            char *tok = read_token(&p, &quoted);

            if (!quoted && argc == 0) {
                const char *alias = get_alias(tok);
                if (alias) {
                    free(tok);
                    char *dup = strdup(alias);
                    char *sp = NULL;
                    char *word = strtok_r(dup, " \t", &sp);
                    while (word && argc < MAX_TOKENS - 1) {
                        seg->argv[argc++] = strdup(word);
                        word = strtok_r(NULL, " \t", &sp);
                    }
                    free(dup);
                    continue;
                }
            }

            if (!quoted && strcmp(tok, "<") == 0) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p) {
                    int q = 0;
                    seg->in_file = read_token(&p, &q);
                }
                free(tok);
                continue;
            } else if (!quoted && (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0)) {
                seg->append = (tok[1] == '>');
                while (*p == ' ' || *p == '\t') p++;
                if (*p) {
                    int q = 0;
                    seg->out_file = read_token(&p, &q);
                }
                free(tok);
                continue;
            }

            if (!quoted && (strchr(tok, '*') || strchr(tok, '?'))) {
                glob_t g;
                int r = glob(tok, 0, NULL, &g);
                if (r == 0 && g.gl_pathc > 0) {
                    for (size_t gi = 0; gi < g.gl_pathc && argc < MAX_TOKENS - 1; gi++) {
                        seg->argv[argc++] = strdup(g.gl_pathv[gi]);
                    }
                    free(tok);
                    globfree(&g);
                    continue;
                }
                globfree(&g);
            }

            seg->argv[argc++] = tok;
        }

        if (argc > 0 && strcmp(seg->argv[argc-1], "&") == 0) {
            background = 1;
            free(seg->argv[argc-1]);
            seg->argv[argc-1] = NULL;
        } else {
            seg->argv[argc] = NULL;
        }

        Command *cmd = calloc(1, sizeof(Command));
        cmd->pipeline = seg_head;
        cmd->background = background;
        cmd->op = op;

        if (!head) head = cmd;
        if (cur_cmd) cur_cmd->next = cmd;
        cur_cmd = cmd;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') break;
        if (!*p) break;
        if (op == OP_NONE) break;
    }

    return head;
}

void free_pipeline(PipelineSegment *p) {
    while (p) {
        PipelineSegment *next = p->next;
        for (int i = 0; p->argv[i]; i++)
            free(p->argv[i]);
        free(p->in_file);
        free(p->out_file);
        free(p);
        p = next;
    }
}

void free_commands(Command *c) {
    while (c) {
        Command *next = c->next;
        free_pipeline(c->pipeline);
        free(c);
        c = next;
    }
}

