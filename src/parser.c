/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 * Parser for shell syntax and command structures.
 */

#define _GNU_SOURCE
#include "parser.h"
#include "builtins.h"
#include "history.h"
#include "util.h"
#include "lexer.h"
#include "arith.h"
#include "execute.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glob.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "scriptargs.h"
#include "options.h"

extern int last_status;
FILE *parse_input = NULL;

struct temp_var { char *name; char *value; struct temp_var *next; };
static struct temp_var *temp_vars = NULL;

struct proc_sub {
    char *path;
    pid_t pid;
    struct proc_sub *next;
};
static struct proc_sub *proc_subs = NULL;

static void add_proc_sub(const char *path, pid_t pid) {
    struct proc_sub *ps = malloc(sizeof(struct proc_sub));
    if (!ps) return;
    ps->path = strdup(path);
    ps->pid = pid;
    ps->next = proc_subs;
    proc_subs = ps;
}

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
/*
 * Read tokens until one of the stop words is encountered,
 * returning the collected text.
 */

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
            int ret = asprintf(&tmp, "%s %s", res, tok);
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
        }
        free(tok);
    }
    return res ? res : strdup("");
}
/*
 * Extract a brace enclosed block while respecting quoted strings.
 */

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

/*
 * Extract a parenthesis enclosed block while respecting quoted strings.
 */

static char *gather_parens(char **p) {
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

static char *process_substitution(char **p, int read_from) {
    char *body = gather_parens(p);
    if (!body)
        return NULL;
    char template[] = "/tmp/vushpsXXXXXX";
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
        add_proc_sub(template, pid);
    } else {
        perror("fork");
        unlink(template);
    }
    free_commands(cmd);
    free(body);
    return strdup(template);
}
/*
 * Parse an if/elif/else clause recursively.
 */

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
/*
 * Parse a while/until loop body.
 */

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

static Command *parse_until_clause(char **p) {
    const char *stop1[] = {"do"};
    char *cond = gather_until(p, stop1, 1, NULL);
    if (!cond) return NULL;
    Command *cond_cmd = parse_line(cond); free(cond);
    const char *stop2[] = {"done"};
    char *body = gather_until(p, stop2, 1, NULL);
    if (!body) { free_commands(cond_cmd); return NULL; }
    Command *body_cmd = parse_line(body); free(body);
    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_UNTIL;
    cmd->cond = cond_cmd;
    cmd->body = body_cmd;
    return cmd;
}
/*
 * Parse a for loop and its word list.
 */

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

static void free_case_items(CaseItem *ci) {
    while (ci) {
        CaseItem *n = ci->next;
        for (int i = 0; i < ci->pattern_count; i++)
            free(ci->patterns[i]);
        free(ci->patterns);
        free_commands(ci->body);
        free(ci);
        ci = n;
    }
}
/*
 * Parse a case statement with patterns and optional fall-through.
 */

static Command *parse_case_clause(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    int q = 0;
    char *word = read_token(p, &q);
    if (!word) return NULL;
    while (**p == ' ' || **p == '\t') (*p)++;
    q = 0;
    char *tok = read_token(p, &q);
    if (!tok || strcmp(tok, "in") != 0) { free(word); free(tok); return NULL; }
    free(tok);

    CaseItem *head = NULL, *tail = NULL;
    while (1) {
        while (**p == ' ' || **p == '\t' || **p == '\n') (*p)++;
        if (strncmp(*p, "esac", 4) == 0) { *p += 4; break; }

        char **patterns = NULL; int pc = 0;
        int done = 0;
        while (!done) {
            while (**p == ' ' || **p == '\t') (*p)++;
            if (**p == '(') { (*p)++; continue; }
            int q = 0; char *ptok = read_token(p, &q); if (!ptok) { free_case_items(head); free(word); return NULL; }
            if (!q && strcmp(ptok, "|") == 0) { free(ptok); continue; }
            if (!q && strcmp(ptok, ")") == 0) { free(ptok); break; }
            size_t len = strlen(ptok);
            if (!q && len > 0 && ptok[len-1] == ')') { ptok[len-1] = '\0'; done = 1; }
            patterns = realloc(patterns, sizeof(char*) * (pc + 1));
            patterns[pc++] = ptok;
            if (done) break;
        }

        const char *stops[] = {";;", ";&"};
        int idx = -1;
        char *body = gather_until(p, stops, 2, &idx);
        if (!body) { free_case_items(head); free(word); return NULL; }
        Command *body_cmd = parse_line(body); free(body);
        CaseItem *ci = calloc(1, sizeof(CaseItem));
        ci->patterns = patterns;
        ci->pattern_count = pc;
        ci->body = body_cmd;
        ci->fall_through = (idx == 1);
        if (!head) head = ci; else tail->next = ci; tail = ci;
    }

    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_CASE;
    cmd->var = word;
    cmd->cases = head;
    return cmd;
}
/*
 * Parse a function definition and return the command or NULL.
 * The body is collected using brace matching and parsed recursively.
 */
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
    }
    *p = savep;
    if (fname) free(fname);
    return NULL;
}

/* Parse a parenthesized subshell command list. */
static Command *parse_subshell(char **p, CmdOp *op_out) {
    char *bodytxt = gather_parens(p);
    if (!bodytxt)
        return NULL;
    Command *body_cmd = parse_line(bodytxt);
    free(bodytxt);
    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_SUBSHELL;
    cmd->group = body_cmd;
    while (**p == ' ' || **p == '\t') (*p)++;
    CmdOp op = OP_NONE;
    if (**p == ';') { op = OP_SEMI; (*p)++; }
    else if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; }
    else if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; }
    cmd->op = op;
    if (op_out) *op_out = op;
    return cmd;
}

/* Parse a brace grouped command list executed in the current shell. */
static Command *parse_brace_group(char **p, CmdOp *op_out) {
    char *bodytxt = gather_braced(p);
    if (!bodytxt)
        return NULL;
    Command *body_cmd = parse_line(bodytxt);
    free(bodytxt);
    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_GROUP;
    cmd->group = body_cmd;
    while (**p == ' ' || **p == '\t') (*p)++;
    CmdOp op = OP_NONE;
    if (**p == ';') { op = OP_SEMI; (*p)++; }
    else if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; }
    else if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; }
    cmd->op = op;
    if (op_out) *op_out = op;
    return cmd;
}

/* Parse a [[ expression ]] conditional */
static Command *parse_conditional(char **p, CmdOp *op_out) {
    if (strncmp(*p, "[[", 2) != 0)
        return NULL;
    *p += 2;
    char **words = NULL;
    int count = 0;
    while (**p) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (!**p)
            break;
        int q = 0;
        char *tok = read_token(p, &q);
        if (!tok) {
            for (int i=0;i<count;i++) free(words[i]);
            free(words);
            return NULL;
        }
        if (!q && strcmp(tok, "]]") == 0) {
            free(tok);
            break;
        }
        words = realloc(words, sizeof(char*)*(count+1));
        words[count++] = tok;
    }
    if (!words && count==0)
        words = NULL; /* nothing */
    while (**p == ' ' || **p == '\t') (*p)++;
    CmdOp op = OP_NONE;
    if (**p == ';') { op = OP_SEMI; (*p)++; }
    else if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; }
    else if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; }
    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_COND;
    cmd->words = words;
    cmd->word_count = count;
    cmd->op = op;
    if (op_out) *op_out = op;
    return cmd;
}

/* Parse top-level control clauses such as if, while, for and case. */
static Command *parse_control_clause(char **p, CmdOp *op_out) {
    Command *cmd = NULL;
    if (strncmp(*p, "if", 2) == 0 && isspace((unsigned char)(*p)[2])) {
        *p += 2;
        cmd = parse_if_clause(p);
    } else if (strncmp(*p, "while", 5) == 0 && isspace((unsigned char)(*p)[5])) {
        *p += 5;
        cmd = parse_while_clause(p);
    } else if (strncmp(*p, "until", 5) == 0 && isspace((unsigned char)(*p)[5])) {
        *p += 5;
        cmd = parse_until_clause(p);
    } else if (strncmp(*p, "for", 3) == 0 && isspace((unsigned char)(*p)[3])) {
        *p += 3;
        cmd = parse_for_clause(p);
    } else if (strncmp(*p, "case", 4) == 0 && isspace((unsigned char)(*p)[4])) {
        *p += 4;
        cmd = parse_case_clause(p);
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

/* Expand an alias when TOK is the first word of a segment.
 * Nested aliases are expanded recursively up to a small depth and
 * previously visited names are tracked to avoid infinite loops. */
#define MAX_ALIAS_DEPTH 10

static void collect_alias_tokens(const char *name, char **out, int *count,
                                 char visited[][MAX_LINE], int depth) {
    if (*count >= MAX_TOKENS - 1)
        return;

    if (depth >= MAX_ALIAS_DEPTH) {
        out[(*count)++] = strdup(name);
        return;
    }

    for (int i = 0; i < depth; i++) {
        if (strcmp(visited[i], name) == 0) {
            out[(*count)++] = strdup(name);
            return;
        }
    }

    const char *alias = get_alias(name);
    if (!alias) {
        out[(*count)++] = strdup(name);
        return;
    }

    strncpy(visited[depth], name, MAX_LINE);
    visited[depth][MAX_LINE - 1] = '\0';

    char *dup = strdup(alias);
    char *sp = NULL;
    char *word = strtok_r(dup, " \t", &sp);
    if (!word) {
        free(dup);
        return;
    }

    collect_alias_tokens(word, out, count, visited, depth + 1);

    word = strtok_r(NULL, " \t", &sp);
    while (word && *count < MAX_TOKENS - 1) {
        out[(*count)++] = strdup(word);
        word = strtok_r(NULL, " \t", &sp);
    }
    free(dup);
}

static int expand_aliases(PipelineSegment *seg, int *argc, char *tok) {
    const char *alias = get_alias(tok);
    if (!alias)
        return 0; /* no alias, leave token untouched */

    char *orig = tok;
    char *tokens[MAX_TOKENS];
    int count = 0;
    char visited[MAX_ALIAS_DEPTH][MAX_LINE];

    collect_alias_tokens(orig, tokens, &count, visited, 0);
    free(orig); /* free only when expansion occurs */

    for (int i = 0; i < count && *argc < MAX_TOKENS - 1; i++)
        seg->argv[(*argc)++] = tokens[i];

    return 1; /* alias expanded */
}

/* Collect a here-doc into a temporary file */
static int collect_here_doc(PipelineSegment *seg, char **p, char *tok, int quoted) {
    if (quoted || strncmp(tok, "<<", 2) != 0)
        return 0;
    if ((tok[2] == '<') || (**p == '<'))
        return 0; /* here-string handled elsewhere */
    char *delim;
    if (tok[2]) {
        delim = strdup(tok + 2);
    } else {
        while (**p == ' ' || **p == '\t') (*p)++;
        int q = 0;
        delim = read_token(p, &q);
        if (!delim) { free(tok); return -1; }
    }
    char template[] = "/tmp/vushXXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) { perror("mkstemp"); free(delim); free(tok); return -1; }
    FILE *tf = fdopen(fd, "w");
    if (!tf) { perror("fdopen"); close(fd); unlink(template); free(delim); free(tok); return -1; }
    char buf[MAX_LINE];
    int found = 0;
    while (fgets(buf, sizeof(buf), parse_input ? parse_input : stdin)) {
        size_t len = strlen(buf);
        if (len && buf[len-1] == '\n') buf[len-1] = '\0';
        if (strcmp(buf, delim) == 0) { found = 1; break; }
        fprintf(tf, "%s\n", buf);
    }
    if (!found) {
        fclose(tf);
        unlink(template);
        free(delim);
        free(tok);
        fprintf(stderr, "syntax error: here-document delimited by end-of-file\n");
        return -1;
    }
    fclose(tf);
    seg->in_file = strdup(template);
    seg->here_doc = 1;
    free(delim);
    free(tok);
    return 1;
}

/* Handle <, >, 2>, &> style redirections */
static int handle_redirection(PipelineSegment *seg, char **p, char *tok, int quoted) {
    if (quoted)
        return 0;
    if ((strcmp(tok, "<<") == 0 && **p == '<') || strncmp(tok, "<<<", 3) == 0) {
        if (strncmp(tok, "<<<", 3) == 0 && tok[3] == '<') {
            free(tok);
            return -1; /* malformed */
        }
        if (strcmp(tok, "<<") == 0 && **p == '<')
            (*p)++; /* skip third '<' */
        while (**p == ' ' || **p == '\t') (*p)++;
        char *word = NULL;
        if (strncmp(tok, "<<<", 3) == 0 && tok[3]) {
            word = strdup(tok + 3);
        } else if (**p) {
            int q = 0;
            word = read_token(p, &q);
            if (!word) { free(tok); return -1; }
        } else {
            word = strdup("");
        }
        char template[] = "/tmp/vushXXXXXX";
        int fd = mkstemp(template);
        if (fd < 0) { perror("mkstemp"); free(word); free(tok); return -1; }
        FILE *tf = fdopen(fd, "w");
        if (!tf) { perror("fdopen"); close(fd); unlink(template); free(word); free(tok); return -1; }
        fprintf(tf, "%s", word);
        fclose(tf);
        seg->in_file = strdup(template);
        seg->here_doc = 1;
        free(word);
        free(tok);
        return 1;
    }
    if (strcmp(tok, "<") == 0) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p) {
            int q = 0;
            seg->in_file = read_token(p, &q);
            if (!seg->in_file) { free(tok); return -1; }
        }
        free(tok);
        return 1;
    }
    if (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0) {
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
                if (!file) { free(tok); return -1; }
                seg->out_file = file;
                seg->err_file = file;
                seg->err_append = seg->append;
            }
        } else if (**p) {
            int q = 0;
            seg->out_file = read_token(p, &q);
            if (!seg->out_file) { free(tok); return -1; }
        }
        free(tok);
        return 1;
    }
    if (strcmp(tok, "2>") == 0 || strcmp(tok, "2>>") == 0) {
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
                if (!file) { free(tok); return -1; }
                seg->err_file = file;
            }
        } else if (**p) {
            int q = 0;
            seg->err_file = read_token(p, &q);
            if (!seg->err_file) { free(tok); return -1; }
        }
        free(tok);
        return 1;
    }
    if (strcmp(tok, "&>") == 0 || strcmp(tok, "&>>") == 0) {
        int app = (tok[2] == '>');
        seg->append = app;
        seg->err_append = app;
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p) {
            int q = 0;
            char *file = read_token(p, &q);
            if (!file) { free(tok); return -1; }
            seg->out_file = file;
            seg->err_file = file;
        }
        free(tok);
        return 1;
    }
    return 0;
}

/* Parse a command line into pipeline segments with alias expansion,
 * redirections, globbing and here-doc handling. */
static Command *parse_pipeline(char **p, CmdOp *op_out) {
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '(')
        return parse_subshell(p, op_out);
    if (**p == '{')
        return parse_brace_group(p, op_out);

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

        if (**p == '<' && *(*p + 1) == '(') {
            (*p)++; /* skip '<' */
            char *path = process_substitution(p, 0);
            if (!path) { free_pipeline(seg_head); return NULL; }
            seg->argv[argc++] = path;
            continue;
        }
        if (**p == '>' && *(*p + 1) == '(') {
            (*p)++; /* skip '>' */
            char *path = process_substitution(p, 1);
            if (!path) { free_pipeline(seg_head); return NULL; }
            seg->argv[argc++] = path;
            continue;
        }

        int quoted = 0;
        char *tok = read_token(p, &quoted);
        if (getenv("VUSH_DEBUG"))
            fprintf(stderr, "parse_pipeline token: '%s'\n", tok ? tok : "(null)");
        if (!tok) { free_pipeline(seg_head); return NULL; }

        if (!quoted && argc == 0 && is_assignment(tok)) {
            char *eq = strchr(tok, '=');
            if (eq && eq[1] == '(' && tok[strlen(tok)-1] != ')') {
                char *assign = strdup(tok);
                char *tmp;
                do {
                    int q2 = 0;
                    tmp = read_token(p, &q2);
                    if (!tmp) { free(assign); free_pipeline(seg_head); return NULL; }
                    char *old = assign;
                    asprintf(&assign, "%s %s", assign, tmp);
                    free(old);
                    free(tmp);
                } while (assign[strlen(assign)-1] != ')');
                free(tok);
                tok = assign;
                eq = strchr(tok, '=');
            }
            seg->assigns = realloc(seg->assigns, sizeof(char *) * (seg->assign_count + 1));
            seg->assigns[seg->assign_count++] = tok;
            if (eq) {
                char *name = strndup(tok, eq - tok);
                if (name) { set_temp_var(name, eq + 1); free(name); }
            }
            continue;
        }

        if (!quoted && argc == 0) {
            if (expand_aliases(seg, &argc, tok))
                continue;
        }

        int handled = collect_here_doc(seg, p, tok, quoted);
        if (handled == -1) { free_pipeline(seg_head); return NULL; }
        if (handled == 1) {
            continue;
        }

        handled = handle_redirection(seg, p, tok, quoted);
        if (handled == -1) { free_pipeline(seg_head); return NULL; }
        if (handled == 1) {
            continue;
        }

        char **btoks = NULL;
        int bcount = 0;
        if (!quoted)
            btoks = expand_braces(tok, &bcount);
        if (!btoks) {
            btoks = malloc(2 * sizeof(char *));
            btoks[0] = tok;
            btoks[1] = NULL;
            bcount = 1;
        } else {
            free(tok);
        }

        for (int bi = 0; bi < bcount && argc < MAX_TOKENS - 1; bi++) {
            char *bt = btoks[bi];
            if (!quoted && (strchr(bt, '*') || strchr(bt, '?'))) {
                glob_t g;
                int r = glob(bt, 0, NULL, &g);
                if (r == 0 && g.gl_pathc > 0) {
                    for (size_t gi = 0; gi < g.gl_pathc && argc < MAX_TOKENS - 1; gi++)
                        seg->argv[argc++] = strdup(g.gl_pathv[gi]);
                    free(bt);
                    globfree(&g);
                    continue;
                }
                globfree(&g);
            }
            seg->argv[argc++] = bt;
        }
        free(btoks);
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
            cmd = parse_conditional(&p, &op);
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
    } else if (c->type == CMD_WHILE || c->type == CMD_UNTIL) {
        free_commands(c->cond);
        free_commands(c->body);
    } else if (c->type == CMD_FOR) {
        free(c->var);
        for (int i = 0; i < c->word_count; i++)
            free(c->words[i]);
        free(c->words);
        free_commands(c->body);
    } else if (c->type == CMD_CASE) {
        free(c->var);
        free_case_items(c->cases);
    } else if (c->type == CMD_FUNCDEF) {
        free(c->var);
        free(c->text);
        free_commands(c->body);
    } else if (c->type == CMD_SUBSHELL || c->type == CMD_GROUP) {
        free_commands(c->group);
    } else if (c->type == CMD_COND) {
        for (int i = 0; i < c->word_count; i++)
            free(c->words[i]);
        free(c->words);
    }
        free(c);
        c = next;
    }
}

