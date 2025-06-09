/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#define _GNU_SOURCE
#include "parser.h"
#include "builtins.h"
#include "history.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glob.h>
#include <pwd.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "scriptargs.h"
#include "options.h"

extern int last_status;
FILE *parse_input = NULL;

struct temp_var { char *name; char *value; struct temp_var *next; };
static struct temp_var *temp_vars = NULL;

static void set_temp_var(const char *name, const char *value) {
    for (struct temp_var *v = temp_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            free(v->value);
            v->value = strdup(value);
            return;
        }
    }
    struct temp_var *v = malloc(sizeof(struct temp_var));
    if (!v) return;
    v->name = strdup(name);
    v->value = strdup(value);
    v->next = temp_vars;
    temp_vars = v;
}

static const char *get_temp_var(const char *name) {
    for (struct temp_var *v = temp_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0)
            return v->value;
    }
    return NULL;
}

static void clear_temp_vars(void) {
    struct temp_var *v = temp_vars;
    while (v) {
        struct temp_var *n = v->next;
        free(v->name);
        free(v->value);
        free(v);
        v = n;
    }
    temp_vars = NULL;
}

static void skip_ws(const char **s) {
    while (isspace((unsigned char)**s)) (*s)++;
}

static long parse_expr(const char **s);

static long parse_primary(const char **s) {
    skip_ws(s);
    if (**s == '(') {
        (*s)++; /* '(' */
        long v = parse_expr(s);
        skip_ws(s);
        if (**s == ')') (*s)++;
        return v;
    }
    if (isalpha((unsigned char)**s) || **s == '_') {
        char name[64]; int n = 0;
        while (isalnum((unsigned char)**s) || **s == '_') {
            if (n < (int)sizeof(name) - 1)
                name[n++] = **s;
            (*s)++;
        }
        name[n] = '\0';
        const char *val = get_temp_var(name);
        if (!val) val = get_shell_var(name);
        if (!val) val = getenv(name);
        return val ? strtol(val, NULL, 10) : 0;
    }
    char *end;
    long v = strtol(*s, &end, 10);
    *s = end;
    return v;
}

static long parse_unary(const char **s) {
    skip_ws(s);
    if (**s == '+' || **s == '-') {
        char op = *(*s)++;
        long v = parse_unary(s);
        return op == '-' ? -v : v;
    }
    return parse_primary(s);
}

static long parse_mul(const char **s) {
    long v = parse_unary(s);
    while (1) {
        skip_ws(s);
        char op = **s;
        if (op == '*' || op == '/' || op == '%') {
            (*s)++;
            long rhs = parse_unary(s);
            if (op == '*') v *= rhs;
            else if (op == '/') v /= rhs;
            else v %= rhs;
        } else break;
    }
    return v;
}

static long parse_add(const char **s) {
    long v = parse_mul(s);
    while (1) {
        skip_ws(s);
        char op = **s;
        if (op == '+' || op == '-') {
            (*s)++;
            long rhs = parse_mul(s);
            if (op == '+') v += rhs;
            else v -= rhs;
        } else break;
    }
    return v;
}

static long parse_cmp(const char **s) {
    long v = parse_add(s);
    while (1) {
        skip_ws(s);
        if (strncmp(*s, "==", 2) == 0) { *s += 2; long r = parse_add(s); v = (v == r); }
        else if (strncmp(*s, "!=", 2) == 0) { *s += 2; long r = parse_add(s); v = (v != r); }
        else if (strncmp(*s, ">=", 2) == 0) { *s += 2; long r = parse_add(s); v = (v >= r); }
        else if (strncmp(*s, "<=", 2) == 0) { *s += 2; long r = parse_add(s); v = (v <= r); }
        else if (**s == '>' ) { (*s)++; long r = parse_add(s); v = (v > r); }
        else if (**s == '<' ) { (*s)++; long r = parse_add(s); v = (v < r); }
        else break;
    }
    return v;
}

static long parse_assign(const char **s) {
    skip_ws(s);
    const char *save = *s;
    if ((isalpha((unsigned char)**s) || **s == '_')) {
        char name[64]; int n = 0;
        while (isalnum((unsigned char)**s) || **s == '_') {
            if (n < (int)sizeof(name) - 1)
                name[n++] = **s;
            (*s)++;
        }
        name[n] = '\0';
        skip_ws(s);
        if (**s == '=') {
            (*s)++;
            long val = parse_assign(s);
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", val);
            set_shell_var(name, buf);
            return val;
        }
    }
    *s = save;
    return parse_cmp(s);
}

static long parse_expr(const char **s) {
    return parse_assign(s);
}

long eval_arith(const char *expr) {
    const char *p = expr;
    long v = parse_expr(&p);
    return v;
}

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

    size_t tlen = strlen(token);
    if (tlen > 4 && strncmp(token, "$((", 3) == 0 && token[tlen-2] == ')' && token[tlen-1] == ')') {
        char *expr = strndup(token + 3, tlen - 5);
        if (!expr) return strdup("");
        long val = eval_arith(expr);
        free(expr);
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", val);
        return strdup(buf);
    }

    if (token[1] == '{') {
        const char *end = strchr(token + 2, '}');
        if (end && end[1] == '\0') {
            size_t len = (size_t)(end - (token + 2));
            char *name = strndup(token + 2, len);
            if (!name) return strdup("");
            const char *val = get_temp_var(name);
            if (!val) val = get_shell_var(name);
            if (!val) val = getenv(name);
            if (!val) {
                if (opt_nounset) {
                    fprintf(stderr, "%s: unbound variable\n", name);
                    last_status = 1;
                }
                val = "";
            }
            free(name);
            return strdup(val);
        }
    }

    if (strcmp(token, "$#") == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", script_argc);
        return strdup(buf);
    }

    if (strcmp(token, "$@") == 0) {
        if (!script_argv || script_argc == 0)
            return strdup("");
        size_t len = 0;
        for (int i = 1; i <= script_argc; i++)
            len += strlen(script_argv[i]) + 1;
        char *res = malloc(len);
        if (!res) return NULL;
        res[0] = '\0';
        for (int i = 1; i <= script_argc; i++) {
            strcat(res, script_argv[i]);
            if (i < script_argc)
                strcat(res, " ");
        }
        return res;
    }

    if (token[1] >= '0' && token[1] <= '9' && token[2] == '\0') {
        int idx = token[1] - '0';
        const char *val = NULL;
        if (script_argv) {
            if (idx == 0)
                val = script_argv[0];
            else if (idx <= script_argc)
                val = script_argv[idx];
        }
        if (!val) {
            if (opt_nounset) {
                fprintf(stderr, "%c: unbound variable\n", token[1]);
                last_status = 1;
            }
            val = "";
        }
        return strdup(val);
    }

    const char *val = get_temp_var(token + 1);
    if (!val) val = get_shell_var(token + 1);
    if (!val) val = getenv(token + 1);
    if (!val) {
        if (opt_nounset) {
            fprintf(stderr, "%s: unbound variable\n", token + 1);
            last_status = 1;
        }
        val = "";
    }
    return strdup(val);
}

char *expand_history(const char *line) {
    const char *p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '!')
        return strdup(line);

    const char *bang = p;
    const char *rest;
    const char *expansion = NULL;
    char pref[MAX_LINE];

    if (p[1] == '!' && (p[2] == '\0' || isspace((unsigned char)p[2]))) {
        expansion = history_last();
        rest = p + 2;
        if (!expansion) {
            fprintf(stderr, "history: event not found\n");
            last_status = 1;
            return NULL;
        }
    } else {
        int n = 0;
        p++;
        while (*p && !isspace((unsigned char)*p) && n < MAX_LINE - 1)
            pref[n++] = *p++;
        pref[n] = '\0';
        expansion = history_find_prefix(pref);
        rest = p;
        if (!expansion) {
            fprintf(stderr, "history: event not found: %s\n", pref);
            last_status = 1;
            return NULL;
        }
    }

    size_t pre_len = (size_t)(bang - line);
    size_t exp_len = strlen(expansion);
    size_t rest_len = strlen(rest);
    char *res = malloc(pre_len + exp_len + rest_len + 1);
    if (!res)
        return NULL;
    memcpy(res, line, pre_len);
    memcpy(res + pre_len, expansion, exp_len);
    memcpy(res + pre_len + exp_len, rest, rest_len + 1);
    return res;
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

    if (**p == '2' && *(*p + 1) == '>') {
        buf[len++] = '2';
        (*p)++;
        buf[len++] = '>';
        (*p)++;
        if (**p == '>') {
            buf[len++] = '>';
            (*p)++;
        }
        buf[len] = '\0';
        return strdup(buf);
    }

    if (**p == '&' && *(*p + 1) == '>') {
        buf[len++] = '&';
        (*p)++;
        buf[len++] = '>';
        (*p)++;
        if (**p == '>') {
            buf[len++] = '>';
            (*p)++;
        }
        buf[len] = '\0';
        return strdup(buf);
    }

    if (**p == '&' && *(*p + 1) != '&' && *(*p + 1) != '>') {
        buf[len++] = '&';
        (*p)++;
        while (**p && **p != ' ' && **p != '\t' && **p != '|' && **p != '<' && **p != '>' && **p != '&' && **p != ';' && len < MAX_LINE - 1) {
            buf[len++] = **p;
            (*p)++;
        }
        buf[len] = '\0';
        return strdup(buf);
    }

    if (**p == '<' && *(*p + 1) == '<') {
        buf[len++] = '<';
        (*p)++;
        buf[len++] = '<';
        (*p)++;
        while (**p && **p != ' ' && **p != '\t' && **p != '|' && **p != '<' && **p != '>' && **p != '&' && **p != ';' && len < MAX_LINE - 1) {
            buf[len++] = **p;
            (*p)++;
        }
        buf[len] = '\0';
        return strdup(buf);
    }

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
        char quote = '\'';
        *quoted = 1;
        do_expand = 0;
        (*p)++; /* skip opening quote */
        while (**p && **p != quote && len < MAX_LINE - 1) {
            buf[len++] = *(*p)++;
        }
        if (**p == quote) {
            (*p)++; /* skip closing quote */
        } else {
            fprintf(stderr, "syntax error: unmatched '%c'\n", quote);
            return NULL;
        }
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
            if (**p == '$' && *(*p + 1) == '(' && *(*p + 2) == '(') {
                /* arithmetic expansion $((expr)) - copy literally for later evaluation */
                int depth = 1;
                if (len < MAX_LINE - 1) buf[len++] = *(*p)++; /* $ */
                if (len < MAX_LINE - 1) buf[len++] = *(*p)++; /* ( */
                if (len < MAX_LINE - 1) buf[len++] = *(*p)++; /* ( */
                while (**p && !(**p == ')' && depth == 0 && *(*p + 1) == ')')) {
                    if (**p == '(') depth++;
                    else if (**p == ')') depth--;
                    if (len < MAX_LINE - 1) buf[len++] = **p;
                    (*p)++;
                }
                if (**p == ')' && *(*p + 1) == ')' && depth == 0) {
                    if (len < MAX_LINE - 1) buf[len++] = *(*p)++;
                    if (len < MAX_LINE - 1) buf[len++] = *(*p)++;
                } else {
                    fprintf(stderr, "syntax error: unmatched ')'\n");
                    return NULL;
                }
                first = 0;
                continue;
            }
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
                if (is_dollar) {
                    if (depth > 0) {
                        fprintf(stderr, "syntax error: unmatched ')'\n");
                        return NULL;
                    }
                } else {
                    if (**p == '`')
                        (*p)++; /* closing backtick */
                    else {
                        fprintf(stderr, "syntax error: unmatched '`'\n");
                        return NULL;
                    }
                }
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
            if (**p == '$' && *(*p + 1) == '{') {
                const char *start = *p;
                (*p) += 2; /* skip ${ */
                char name[MAX_LINE];
                int n = 0;
                while (**p && **p != '}' && n < MAX_LINE - 1) {
                    name[n++] = **p;
                    (*p)++;
                }
                if (**p == '}') {
                    (*p)++; /* skip closing } */
                    name[n] = '\0';
                    char varbuf[MAX_LINE];
                    snprintf(varbuf, sizeof(varbuf), "${%s}", name);
                    char *res = expand_var(varbuf);
                    for (int ci = 0; res[ci] && len < MAX_LINE - 1; ci++)
                        buf[len++] = res[ci];
                    free(res);
                    first = 0;
                    continue;
                }
                /* unmatched '{' - treat literally */
                *p = (char *)start;
            }
            if (in_double && **p == '"') {
                (*p)++; /* end quote */
                break;
            }
            if (len < MAX_LINE - 1) buf[len++] = **p;
            (*p)++;
            first = 0;
        }
        if (in_double && *(*p - 1) != '"') {
            fprintf(stderr, "syntax error: unmatched '\"'\n");
            return NULL;
        }
    }

    buf[len] = '\0';
    return do_expand ? expand_var(buf) : strdup(buf);
}

char *expand_prompt(const char *prompt) {
    if (!prompt)
        return strdup("");
    size_t len = strlen(prompt);
    char tmp[len + 3];
    tmp[0] = '"';
    memcpy(tmp + 1, prompt, len);
    tmp[len + 1] = '"';
    tmp[len + 2] = '\0';
    char *p = tmp;
    int quoted = 0;
    char *res = read_token(&p, &quoted);
    if (!res)
        return strdup("");
    return res;
}

static int is_assignment(const char *tok) {
    const char *eq = strchr(tok, '=');
    if (!eq || eq == tok)
        return 0;
    for (const char *p = tok; p < eq; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_'))
            return 0;
    }
    if (isdigit((unsigned char)tok[0]))
        return 0;
    return 1;
}

static char *gather_until(char **p, const char **stops, int nstops, int *idx) {
    char *res = NULL;
    if (idx) *idx = -1;
    while (**p) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '\0') break;
        int quoted = 0;
        char *tok = read_token(p, &quoted);
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
            asprintf(&tmp, "%s %s", res, tok);
            free(res);
            res = tmp;
        } else {
            res = strdup(tok);
        }
        free(tok);
    }
    return res ? res : strdup("");
}

static char *gather_braced(char **p) {
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

static Command *parse_if_clause(char **p) {
    const char *stop1[] = {"then"};
    char *cond = gather_until(p, stop1, 1, NULL);
    if (!cond) return NULL;
    Command *cond_cmd = parse_line(cond);
    free(cond);
    int idx = -1;
    const char *stop2[] = {"else", "elif", "fi"};
    char *body = gather_until(p, stop2, 3, &idx);
    if (!body) { free_commands(cond_cmd); return NULL; }
    Command *body_cmd = parse_line(body); free(body);
    Command *else_cmd = NULL;
    if (idx == 0) {
        const char *stop3[] = {"fi"};
        char *els = gather_until(p, stop3, 1, NULL);
        if (!els) { free_commands(cond_cmd); free_commands(body_cmd); return NULL; }
        else_cmd = parse_line(els); free(els);
    } else if (idx == 1) {
        else_cmd = parse_if_clause(p);
    }
    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_IF;
    cmd->cond = cond_cmd;
    cmd->body = body_cmd;
    cmd->else_part = else_cmd;
    return cmd;
}

static Command *parse_while_clause(char **p) {
    const char *stop1[] = {"do"};
    char *cond = gather_until(p, stop1, 1, NULL);
    if (!cond) return NULL;
    Command *cond_cmd = parse_line(cond); free(cond);
    const char *stop2[] = {"done"};
    char *body = gather_until(p, stop2, 1, NULL);
    if (!body) { free_commands(cond_cmd); return NULL; }
    Command *body_cmd = parse_line(body); free(body);
    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_WHILE;
    cmd->cond = cond_cmd;
    cmd->body = body_cmd;
    return cmd;
}

static Command *parse_for_clause(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    int q = 0;
    char *var = read_token(p, &q);
    if (!var || q) { free(var); return NULL; }
    while (**p == ' ' || **p == '\t') (*p)++;
    q = 0;
    char *tok = read_token(p, &q);
    if (!tok || strcmp(tok, "in") != 0) { free(var); free(tok); return NULL; }
    free(tok);
    char **words = NULL; int count = 0;
    const char *stop[] = {"do"};
    while (1) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '\0') { free(var); free(words); return NULL; }
        q = 0;
        char *w = read_token(p, &q);
        if (!w) { free(var); free(words); return NULL; }
        if (!q && strcmp(w, "do") == 0) { free(w); break; }
        words = realloc(words, sizeof(char *) * (count + 1));
        words[count++] = w;
    }
    const char *stop2[] = {"done"};
    char *body = gather_until(p, stop2, 1, NULL);
    if (!body) { free(var); for (int i=0;i<count;i++) free(words[i]); free(words); return NULL; }
    Command *body_cmd = parse_line(body); free(body);
    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_FOR;
    cmd->var = var;
    cmd->words = words;
    cmd->word_count = count;
    cmd->body = body_cmd;
    return cmd;
}

/* Parse a function definition and return the command or NULL */
static Command *parse_function_def(char **p, CmdOp *op_out) {
    char *savep = *p;
    int qfunc = 0;
    char *fname = read_token(p, &qfunc);
    if (fname && !qfunc && **p == '(' && *(*p + 1) == ')') {
        *p += 2;
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '{') {
            char *bodytxt = gather_braced(p);
            if (!bodytxt) { free(fname); return NULL; }
            Command *body_cmd = parse_line(bodytxt);
            Command *cmd = calloc(1, sizeof(Command));
            cmd->type = CMD_FUNCDEF;
            cmd->var = fname;
            cmd->text = bodytxt;
            cmd->body = body_cmd;
            while (**p == ' ' || **p == '\t') (*p)++;
            CmdOp op = OP_NONE;
            if (**p == ';') { op = OP_SEMI; (*p)++; }
            else if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; }
            else if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; }
            cmd->op = op;
            if (op_out) *op_out = op;
            return cmd;
        }
        *p = savep;
    }
    if (fname) free(fname);
    return NULL;
}

/* Parse control clauses like if/while/for */
static Command *parse_control_clause(char **p, CmdOp *op_out) {
    Command *cmd = NULL;
    if (strncmp(*p, "if", 2) == 0 && isspace((unsigned char)(*p)[2])) {
        *p += 2;
        cmd = parse_if_clause(p);
    } else if (strncmp(*p, "while", 5) == 0 && isspace((unsigned char)(*p)[5])) {
        *p += 5;
        cmd = parse_while_clause(p);
    } else if (strncmp(*p, "for", 3) == 0 && isspace((unsigned char)(*p)[3])) {
        *p += 3;
        cmd = parse_for_clause(p);
    }
    if (!cmd)
        return NULL;
    while (**p == ' ' || **p == '\t') (*p)++;
    CmdOp op = OP_NONE;
    if (**p == ';') { op = OP_SEMI; (*p)++; }
    else if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; }
    else if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; }
    cmd->op = op;
    if (op_out) *op_out = op;
    return cmd;
}

/* Parse a regular command pipeline */
static Command *parse_pipeline(char **p, CmdOp *op_out) {
    PipelineSegment *seg_head = calloc(1, sizeof(PipelineSegment));
    seg_head->dup_out = -1;
    seg_head->dup_err = -1;
    seg_head->assigns = NULL;
    seg_head->assign_count = 0;
    PipelineSegment *seg = seg_head;
    int argc = 0;
    int background = 0;
    CmdOp op = OP_NONE;

    while (**p && argc < MAX_TOKENS - 1) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '\0' || **p == '#') { op = OP_NONE; break; }

        if (**p == ';') { op = OP_SEMI; (*p)++; break; }
        if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; break; }
        if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; break; }

        if (**p == '|') {
            seg->argv[argc] = NULL;
            PipelineSegment *next = calloc(1, sizeof(PipelineSegment));
            next->dup_out = -1;
            next->dup_err = -1;
            next->assigns = NULL;
            next->assign_count = 0;
            seg->next = next;
            seg = next;
            argc = 0;
            (*p)++;
            clear_temp_vars();
            continue;
        }

        int quoted = 0;
        char *tok = read_token(p, &quoted);
        if (!tok) { free_pipeline(seg_head); return NULL; }

        if (!quoted && argc == 0 && is_assignment(tok)) {
            seg->assigns = realloc(seg->assigns, sizeof(char *) * (seg->assign_count + 1));
            seg->assigns[seg->assign_count++] = tok;
            char *eq = strchr(tok, '=');
            if (eq) {
                char *name = strndup(tok, eq - tok);
                if (name) { set_temp_var(name, eq + 1); free(name); }
            }
            continue;
        }

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

        if (!quoted && strncmp(tok, "<<", 2) == 0) {
            char *delim;
            if (tok[2]) {
                delim = strdup(tok + 2);
            } else {
                while (**p == ' ' || **p == '\t') (*p)++;
                int q = 0;
                delim = read_token(p, &q);
                if (!delim) { free(tok); free_pipeline(seg_head); return NULL; }
            }
            char template[] = "/tmp/vushXXXXXX";
            int fd = mkstemp(template);
            if (fd < 0) { perror("mkstemp"); free(delim); free(tok); free_pipeline(seg_head); return NULL; }
            FILE *tf = fdopen(fd, "w");
            if (!tf) { perror("fdopen"); close(fd); unlink(template); free(delim); free(tok); free_pipeline(seg_head); return NULL; }
            char buf[MAX_LINE];
            while (fgets(buf, sizeof(buf), parse_input ? parse_input : stdin)) {
                size_t len = strlen(buf);
                if (len && buf[len-1] == '\n') buf[len-1] = '\0';
                if (strcmp(buf, delim) == 0) break;
                fprintf(tf, "%s\n", buf);
            }
            fclose(tf);
            seg->in_file = strdup(template);
            seg->here_doc = 1;
            free(delim);
            free(tok);
            continue;
        } else if (!quoted && strcmp(tok, "<") == 0) {
            while (**p == ' ' || **p == '\t') (*p)++;
            if (**p) {
                int q = 0;
                seg->in_file = read_token(p, &q);
                if (!seg->in_file) { free(tok); free_pipeline(seg_head); return NULL; }
            }
            free(tok);
            continue;
        } else if (!quoted && (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0)) {
            seg->append = (tok[1] == '>');
            while (**p == ' ' || **p == '\t') (*p)++;
            if (**p == '&') {
                (*p)++;
                while (**p == ' ' || **p == '\t') (*p)++;
                if (isdigit(**p)) {
                    seg->dup_out = strtol(*p, p, 10);
                } else if (**p) {
                    int q = 0;
                    char *file = read_token(p, &q);
                    if (!file) { free(tok); free_pipeline(seg_head); return NULL; }
                    seg->out_file = file;
                    seg->err_file = file;
                    seg->err_append = seg->append;
                }
            } else if (**p) {
                int q = 0;
                seg->out_file = read_token(p, &q);
                if (!seg->out_file) { free(tok); free_pipeline(seg_head); return NULL; }
            }
            free(tok);
            continue;
        } else if (!quoted && (strcmp(tok, "2>") == 0 || strcmp(tok, "2>>") == 0)) {
            seg->err_append = (tok[2] == '>');
            while (**p == ' ' || **p == '\t') (*p)++;
            if (**p == '&') {
                (*p)++;
                while (**p == ' ' || **p == '\t') (*p)++;
                if (isdigit(**p)) {
                    seg->dup_err = strtol(*p, p, 10);
                } else if (**p) {
                    int q = 0;
                    char *file = read_token(p, &q);
                    if (!file) { free(tok); free_pipeline(seg_head); return NULL; }
                    seg->err_file = file;
                }
            } else if (**p) {
                int q = 0;
                seg->err_file = read_token(p, &q);
                if (!seg->err_file) { free(tok); free_pipeline(seg_head); return NULL; }
            }
            free(tok);
            continue;
        } else if (!quoted && (strcmp(tok, "&>") == 0 || strcmp(tok, "&>>") == 0)) {
            int app = (tok[2] == '>');
            seg->append = app;
            seg->err_append = app;
            while (**p == ' ' || **p == '\t') (*p)++;
            if (**p) {
                int q = 0;
                char *file = read_token(p, &q);
                if (!file) { free(tok); free_pipeline(seg_head); return NULL; }
                seg->out_file = file;
                seg->err_file = file;
            }
            free(tok);
            continue;
        }

        if (!quoted && (strchr(tok, '*') || strchr(tok, '?'))) {
            glob_t g;
            int r = glob(tok, 0, NULL, &g);
            if (r == 0 && g.gl_pathc > 0) {
                for (size_t gi = 0; gi < g.gl_pathc && argc < MAX_TOKENS - 1; gi++)
                    seg->argv[argc++] = strdup(g.gl_pathv[gi]);
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

    clear_temp_vars();

    if (op_out) *op_out = op;
    return cmd;
}

/* Read a logical line supporting backslash continuations */
char *read_continuation_lines(FILE *f, char *buf, size_t size) {
    return read_logical_line(f, buf, size);
}

Command *parse_line(char *line) {
    char *p = line;
    Command *head = NULL, *cur_cmd = NULL;

    clear_temp_vars();

    while (1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#')
            break;

        CmdOp op = OP_NONE;
        Command *cmd = parse_function_def(&p, &op);
        if (!cmd)
            cmd = parse_control_clause(&p, &op);
        if (!cmd)
            cmd = parse_pipeline(&p, &op);
        if (!cmd) {
            free_commands(head);
            last_status = 1;
            return NULL;
        }

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
        for (int i = 0; i < p->assign_count; i++)
            free(p->assigns[i]);
        free(p->assigns);
        free(p->in_file);
        free(p->out_file);
        if (p->err_file && p->err_file != p->out_file)
            free(p->err_file);
        free(p);
        p = next;
    }
}

void free_commands(Command *c) {
    while (c) {
        Command *next = c->next;
    if (c->type == CMD_PIPELINE) {
        free_pipeline(c->pipeline);
    } else if (c->type == CMD_IF) {
        free_commands(c->cond);
        free_commands(c->body);
        free_commands(c->else_part);
    } else if (c->type == CMD_WHILE) {
        free_commands(c->cond);
        free_commands(c->body);
    } else if (c->type == CMD_FOR) {
        free(c->var);
        for (int i = 0; i < c->word_count; i++)
            free(c->words[i]);
        free(c->words);
        free_commands(c->body);
    } else if (c->type == CMD_FUNCDEF) {
        free(c->var);
        free(c->text);
        free_commands(c->body);
    }
        free(c);
        c = next;
    }
}

