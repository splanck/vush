/*
 * Pipeline parsing routines extracted from parser.c
 */
#define _GNU_SOURCE
#include "parser.h"
#include "lexer.h"
#include "builtins.h"
#include "util.h"
#include "execute.h"
#include "options.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glob.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

extern char *process_substitution(char **p, int read_from);
extern char *gather_dbl_parens(char **p); /* for arithmetic for loops */
extern char *trim_ws(const char *s);
extern int last_status;

#define MAX_ALIAS_DEPTH 10

/* temporary variable handling */
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

/* alias expansion helpers */
static int collect_alias_tokens(const char *name, char **out, int *count,
                                char visited[][MAX_LINE], int depth) {
    if (*count >= MAX_TOKENS - 1)
        return 0;

    int start = *count;

    if (depth >= MAX_ALIAS_DEPTH) {
        char *cp = strdup(name);
        if (!cp)
            return -1;
        out[(*count)++] = cp;
        return 0;
    }

    for (int i = 0; i < depth; i++) {
        if (strcmp(visited[i], name) == 0) {
            char *cp = strdup(name);
            if (!cp)
                return -1;
            out[(*count)++] = cp;
            return 0;
        }
    }

    const char *alias = get_alias(name);
    if (!alias) {
        char *cp = strdup(name);
        if (!cp)
            return -1;
        out[(*count)++] = cp;
        return 0;
    }

    strncpy(visited[depth], name, MAX_LINE);
    visited[depth][MAX_LINE - 1] = '\0';

    char *dup = strdup(alias);
    if (!dup)
        return -1;

    char *sp = NULL;
    char *word = strtok_r(dup, " \t", &sp);
    if (!word) {
        free(dup);
        return 0;
    }

    if (collect_alias_tokens(word, out, count, visited, depth + 1) == -1) {
        free(dup);
        goto error;
    }

    word = strtok_r(NULL, " \t", &sp);
    while (word && *count < MAX_TOKENS - 1) {
        char *cp = strdup(word);
        if (!cp) {
            free(dup);
            goto error;
        }
        out[(*count)++] = cp;
        word = strtok_r(NULL, " \t", &sp);
    }

    free(dup);
    return 0;

error:
    for (int i = start; i < *count; i++)
        free(out[i]);
    *count = start;
    return -1;
}

static int expand_aliases_in_segment(PipelineSegment *seg, int *argc, char *tok) {
    const char *alias = get_alias(tok);
    if (!alias)
        return 0;

    char *orig = tok;
    char *tokens[MAX_TOKENS];
    int count = 0;
    char visited[MAX_ALIAS_DEPTH][MAX_LINE];

    if (collect_alias_tokens(orig, tokens, &count, visited, 0) == -1) {
        free(orig);
        for (int i = 0; i < count; i++)
            free(tokens[i]);
        return -1;
    }

    free(orig);
    int i = 0;
    for (; i < count && *argc < MAX_TOKENS - 1; i++) {
        seg->argv[*argc] = tokens[i];
        seg->expand[*argc] = 1;
        (*argc)++;
    }
    for (; i < count; i++)
        free(tokens[i]);
    return 1;
}

static int process_here_doc(PipelineSegment *seg, char **p, char *tok, int quoted) {
    if (quoted || strncmp(tok, "<<", 2) != 0)
        return 0;
    if ((tok[2] == '<') || (**p == '<'))
        return 0;
    int strip_tabs = 0;
    char *rest = tok + 2;
    if (*rest == '-') {
        strip_tabs = 1;
        rest++;
    }
    char *delim;
    if (*rest) {
        delim = strdup(rest);
        if (!delim) {
            free(tok);
            return -1;
        }
    } else {
        while (**p == ' ' || **p == '\t') (*p)++;
        int q = 0; int de = 1;
        delim = read_token(p, &q, &de);
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
        char *line = buf;
        if (strip_tabs) {
            while (*line == '\t') line++;
        }
        if (strcmp(line, delim) == 0) { found = 1; break; }
        fprintf(tf, "%s\n", line);
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
    if (!seg->in_file) {
        unlink(template);
        free(delim);
        free(tok);
        return -1;
    }
    seg->here_doc = 1;
    free(delim);
    free(tok);
    return 1;
}

static int parse_here_string(PipelineSegment *seg, char **p, char *tok) {
    if (!((strcmp(tok, "<<") == 0 && **p == '<') || strncmp(tok, "<<<", 3) == 0))
        return 0;
    if (strncmp(tok, "<<<", 3) == 0 && tok[3] == '<') {
        free(tok);
        return -1;
    }
    if (strcmp(tok, "<<") == 0 && **p == '<')
        (*p)++;
    while (**p == ' ' || **p == '\t') (*p)++;
    char *word = NULL;
    if (strncmp(tok, "<<<", 3) == 0 && tok[3]) {
        word = strdup(tok + 3);
        if (!word) { free(tok); return -1; }
    } else if (**p) {
        int q = 0; int de = 1;
        word = read_token(p, &q, &de);
        if (!word) { free(tok); return -1; }
    } else {
        word = strdup("");
        if (!word) { free(tok); return -1; }
    }
    char template[] = "/tmp/vushXXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) { perror("mkstemp"); free(word); free(tok); return -1; }
    FILE *tf = fdopen(fd, "w");
    if (!tf) { perror("fdopen"); close(fd); unlink(template); free(word); free(tok); return -1; }
    fprintf(tf, "%s", word);
    fclose(tf);
    seg->in_file = strdup(template);
    if (!seg->in_file) {
        unlink(template);
        free(word);
        free(tok);
        return -1;
    }
    seg->here_doc = 1;
    free(word);
    free(tok);
    return 1;
}

static int parse_input_redirect(PipelineSegment *seg, char **p, char *tok) {
    char *t = tok;
    int fd = STDIN_FILENO;
    if (isdigit((unsigned char)*t)) {
        fd = strtol(t, &t, 10);
    }
    if (strcmp(t, "<") != 0)
        return 0;
    seg->in_fd = fd;
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p) {
        int q = 0; int de = 1;
        seg->in_file = read_token(p, &q, &de);
        if (!seg->in_file) { free(tok); return -1; }
    }
    free(tok);
    return 1;
}

static int parse_output_redirect(PipelineSegment *seg, char **p, char *tok) {
    char *t = tok;
    int fd = STDOUT_FILENO;
    if (isdigit((unsigned char)*t)) {
        fd = strtol(t, &t, 10);
        if (fd == 2 && (strcmp(t, ">") == 0 || strcmp(t, ">>") == 0))
            return 0; /* let parse_error_redirect handle */
    }
    if (!(strcmp(t, ">") == 0 || strcmp(t, ">>") == 0 || strcmp(t, ">|") == 0))
        return 0;
    seg->out_fd = fd;
    seg->append = (t[1] == '>');
    seg->force = (strcmp(t, ">|") == 0);
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '&') {
        (*p)++;
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '-') {
            seg->close_out = 1;
            (*p)++;
        } else if (isdigit(**p)) {
            seg->dup_out = strtol(*p, p, 10);
        } else if (**p) {
            int q = 0; int de = 1;
            char *file = read_token(p, &q, &de);
            if (!file) { free(tok); return -1; }
            seg->out_file = file;
            seg->err_file = file;
            seg->err_append = seg->append;
        }
    } else if (**p) {
        int q = 0; int de = 1;
        seg->out_file = read_token(p, &q, &de);
        if (!seg->out_file) { free(tok); return -1; }
    }
    free(tok);
    return 1;
}

static int parse_error_redirect(PipelineSegment *seg, char **p, char *tok) {
    if (!(strcmp(tok, "2>") == 0 || strcmp(tok, "2>>") == 0))
        return 0;
    seg->err_append = (tok[2] == '>');
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '&') {
        (*p)++;
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '-') {
            seg->close_err = 1;
            (*p)++;
        } else if (isdigit(**p)) {
            seg->dup_err = strtol(*p, p, 10);
        } else if (**p) {
            int q = 0; int de = 1;
            char *file = read_token(p, &q, &de);
            if (!file) { free(tok); return -1; }
            seg->err_file = file;
        }
    } else if (**p) {
        int q = 0; int de = 1;
        seg->err_file = read_token(p, &q, &de);
        if (!seg->err_file) { free(tok); return -1; }
    }
    free(tok);
    return 1;
}

static int parse_combined_redirect(PipelineSegment *seg, char **p, char *tok) {
    if (!(strcmp(tok, "&>") == 0 || strcmp(tok, "&>>") == 0))
        return 0;
    int app = (tok[2] == '>');
    seg->append = app;
    seg->err_append = app;
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p) {
        int q = 0; int de = 1;
        char *file = read_token(p, &q, &de);
        if (!file) { free(tok); return -1; }
        seg->out_file = file;
        seg->err_file = file;
    }
    /* Ensure err_file always mirrors out_file for combined redirection */
    if (!seg->err_file)
        seg->err_file = seg->out_file;
    free(tok);
    return 1;
}

static int parse_redirection(PipelineSegment *seg, char **p, char *tok, int quoted) {
    if (quoted)
        return 0;
    int r;
    r = parse_here_string(seg, p, tok);
    if (r) return r;
    r = parse_input_redirect(seg, p, tok);
    if (r) return r;
    r = parse_output_redirect(seg, p, tok);
    if (r) return r;
    r = parse_error_redirect(seg, p, tok);
    if (r) return r;
    r = parse_combined_redirect(seg, p, tok);
    if (r) return r;
    return 0;
}

static int handle_assignment_or_alias(PipelineSegment *seg, int *argc, char **p,
                                      char **tok_ptr, int quoted) {
    char *tok = *tok_ptr;
    if (!quoted && (*argc == 0 || opt_keyword) && is_assignment(tok)) {
        char *eq = strchr(tok, '=');
        if (eq && eq[1] == '(' && tok[strlen(tok) - 1] != ')') {
            char *assign = strdup(tok);
            char *tmp;
            do {
                int q2 = 0; int de2 = 1;
                tmp = read_token(p, &q2, &de2);
                if (!tmp) { free(assign); free(tok); return -1; }
                char *new_assign = NULL;
                int ret = asprintf(&new_assign, "%s %s", assign, tmp);
                if (ret < 0 || !new_assign) {
                    free(assign);
                    free(tmp);
                    free(tok);
                    return -1;
                }
                free(assign);
                assign = new_assign;
                free(tmp);
            } while (assign[strlen(assign) - 1] != ')');
            free(tok);
            tok = assign;
            eq = strchr(tok, '=');
        }
        char **new_assigns = realloc(seg->assigns,
                                    sizeof(char *) * (seg->assign_count + 1));
        if (!new_assigns) {
            free(tok);
            return -1;
        }
        seg->assigns = new_assigns;
        seg->assigns[seg->assign_count++] = tok;
        if (eq) {
            char *name = strndup(tok, eq - tok);
            if (name) { set_temp_var(name, eq + 1); free(name); }
        }
        *tok_ptr = NULL;
        return 1;
    }
    if (!quoted && *argc == 0) {
        int r = expand_aliases_in_segment(seg, argc, tok);
        if (r == -1)
            return -1;
        if (r == 1) {
            *tok_ptr = NULL;
            return 1;
        }
    }
    return 0;
}

static void finalize_segment(PipelineSegment *seg, int argc, int *background) {
    if (argc > 0 && strcmp(seg->argv[argc - 1], "&") == 0) {
        *background = 1;
        free(seg->argv[argc - 1]);
        seg->argv[argc - 1] = NULL;
    } else {
        seg->argv[argc] = NULL;
        seg->expand[argc] = 0;
    }
}

static int start_new_segment(char **p, PipelineSegment **seg_ptr, int *argc) {
    PipelineSegment *seg = *seg_ptr;
    seg->argv[*argc] = NULL;
    seg->expand[*argc] = 0;
    PipelineSegment *next = calloc(1, sizeof(PipelineSegment));
    if (!next) {
        perror("calloc");
        return -1;
    }
    next->dup_out = -1;
    next->dup_err = -1;
    next->out_fd = STDOUT_FILENO;
    next->in_fd = STDIN_FILENO;
    next->assigns = NULL;
    next->assign_count = 0;
    seg->next = next;
    *seg_ptr = next;
    *argc = 0;
    (*p)++;
    clear_temp_vars();
    return 0;
}

static char **expand_token_braces(char *tok, int quoted, int *count) {
    if (!quoted && !(tok[0] == '$' && tok[1] == '{')) {
        char **btoks = expand_braces(tok, count);
        free(tok);
        return btoks;
    }

    char **btoks = malloc(2 * sizeof(char *));
    if (!btoks) {
        free(tok);
        return NULL;
    }
    btoks[0] = tok;
    btoks[1] = NULL;
    if (count) *count = 1;
    return btoks;
}

static int parse_pipeline_segment(char **p, PipelineSegment **seg_ptr, int *argc,
                                  CmdOp *op_out) {
    PipelineSegment *seg = *seg_ptr;
    CmdOp op = OP_NONE;
    while (**p && *argc < MAX_TOKENS - 1) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '\0' || **p == '#') { op = OP_NONE; break; }
        if (**p == ';') { op = OP_SEMI; (*p)++; break; }
        if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; break; }
        if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; break; }
        if (**p == '|') {
            if (start_new_segment(p, &seg, argc) == -1)
                return -1;
            continue;
        }
        if (**p == '<' && *(*p + 1) == '(') {
            (*p)++;
            char *path = process_substitution(p, 0);
            if (!path) return -1;
            seg->argv[*argc] = path;
            seg->expand[*argc] = 0;
            (*argc)++;
            continue;
        }
        if (**p == '>' && *(*p + 1) == '(') {
            (*p)++;
            char *path = process_substitution(p, 1);
            if (!path) return -1;
            seg->argv[*argc] = path;
            seg->expand[*argc] = 0;
            (*argc)++;
            continue;
        }
        int quoted = 0; int de_tok = 1;
        char *tok = read_token(p, &quoted, &de_tok);
        if (!tok)
            return -1;
        if (!quoted) {
            const char *s = tok;
            int all_digits = (*s != '\0');
            for (; *s && all_digits; s++)
                if (!isdigit((unsigned char)*s))
                    all_digits = 0;
            if (all_digits && (**p == '>' || **p == '<')) {
                int q2 = 0; int de2 = 1;
                char *op = read_token(p, &q2, &de2);
                if (!op) { free(tok); return -1; }
                char *nt = NULL;
                int ret = asprintf(&nt, "%s%s", tok, op);
                if (ret < 0 || !nt) {
                    free(op);
                    free(tok);
                    return -1;
                }
                free(op);
                free(tok);
                tok = nt;
            }
        }
        if (getenv("VUSH_DEBUG"))
            fprintf(stderr, "parse_pipeline token: '%s'\n", tok ? tok : "(null)");
        if (!tok) return -1;
        int h = handle_assignment_or_alias(seg, argc, p, &tok, quoted);
        if (h == -1) return -1;
        if (h == 1) { if (tok) free(tok); continue; }
        h = process_here_doc(seg, p, tok, quoted);
        if (h == -1) return -1;
        if (h == 1) continue;
        h = parse_redirection(seg, p, tok, quoted);
        if (h == -1) return -1;
        if (h == 1) continue;
        int bcount = 0;
        char **btoks = expand_token_braces(tok, quoted, &bcount);
        if (!btoks)
            return -1;
        int bi = 0;
        for (; bi < bcount && *argc < MAX_TOKENS - 1; bi++) {
            char *bt = btoks[bi];
            if (!quoted && !opt_noglob && (strchr(bt, '*') || strchr(bt, '?')) ) {
                glob_t g;
                int r = glob(bt, 0, NULL, &g);
                if (r == 0 && g.gl_pathc > 0) {
                    size_t start = (size_t)*argc;
                    for (size_t gi = 0; gi < g.gl_pathc && *argc < MAX_TOKENS - 1; gi++) {
                        char *dup = strdup(g.gl_pathv[gi]);
                        if (!dup) {
                            while ((size_t)*argc > start) {
                                free(seg->argv[--(*argc)]);
                                seg->argv[*argc] = NULL;
                            }
                            free(bt);
                            globfree(&g);
                            for (int bj = bi + 1; bj < bcount; bj++)
                                free(btoks[bj]);
                            free(btoks);
                            return -1;
                        }
                        seg->argv[*argc] = dup;
                        seg->expand[*argc] = de_tok;
                        (*argc)++;
                    }
                    free(bt);
                    globfree(&g);
                    continue;
                }
                globfree(&g);
            }
            seg->argv[*argc] = bt;
            seg->expand[*argc] = de_tok;
            (*argc)++;
        }
        for (int bj = bi; bj < bcount; bj++)
            free(btoks[bj]);
        free(btoks);
    }
    if (op_out) *op_out = op;
    *seg_ptr = seg;
    return 0;
}

static Command *parse_pipeline(char **p, CmdOp *op_out) {
    while (**p == ' ' || **p == '\t') (*p)++;
    int negate = 0;
    if (**p == '!') {
        negate = 1;
        (*p)++;
        while (**p == ' ' || **p == '\t') (*p)++;
    }
    if (**p == '(')
    {
        Command *cmd = parse_subshell(p, op_out);
        if (cmd) cmd->negate = negate;
        return cmd;
    }
    if (**p == '{')
    {
        Command *cmd = parse_brace_group(p, op_out);
        if (cmd) cmd->negate = negate;
        return cmd;
    }
    PipelineSegment *seg_head = calloc(1, sizeof(PipelineSegment));
    if (!seg_head)
        return NULL;
    seg_head->dup_out = -1;
    seg_head->dup_err = -1;
    seg_head->out_fd = STDOUT_FILENO;
    seg_head->in_fd = STDIN_FILENO;
    seg_head->assigns = NULL;
    seg_head->assign_count = 0;
    PipelineSegment *seg = seg_head;
    int argc = 0;
    int background = 0;
    CmdOp op = OP_NONE;
    if (parse_pipeline_segment(p, &seg, &argc, &op) == -1) {
        free_pipeline(seg_head);
        return NULL;
    }
    finalize_segment(seg, argc, &background);
    Command *cmd = calloc(1, sizeof(Command));
    if (!cmd) {
        free_pipeline(seg_head);
        return NULL;
    }
    cmd->pipeline = seg_head;
    cmd->background = background;
    cmd->negate = negate;
    cmd->op = op;
    clear_temp_vars();
    if (op_out) *op_out = op;
    return cmd;
}

char *read_continuation_lines(FILE *f, char *buf, size_t size) {
    return read_logical_line(f, buf, size);
}

Command *parse_line(char *line) {
    char *p = line;
    Command *head = NULL, *cur_cmd = NULL;
    parse_need_more = 0;
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
            cleanup_proc_subs();
            if (!parse_need_more)
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

