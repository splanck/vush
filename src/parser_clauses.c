/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Parsing of shell clauses.
 */

/*
 * Parsing of shell control clauses extracted from parser.c
 */
#define _GNU_SOURCE
#include "parser.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "strarray.h"
#include "cleanup.h"
#include "util.h"
#include "options.h"

/* helpers from parser_utils */
extern char *gather_until(char **p, const char **stops, int nstops, int *idx);
extern char *gather_braced(char **p);
extern char *gather_parens(char **p);
extern char *gather_dbl_parens(char **p);
extern char *gather_until_done(char **p);
extern char *trim_ws(const char *s);

/* Forward declaration used by parse_case_clause and free_commands */
void free_case_items(CaseItem *ci);

/* Parse an if/elif/else clause starting at *p. */
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
    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free_commands(cond_cmd);
        free_commands(body_cmd);
        free_commands(else_cmd);
        return NULL;
    }
    cmd->type = CMD_IF;
    cmd->cond = cond_cmd;
    cmd->body = body_cmd;
    cmd->else_part = else_cmd;
    return cmd;
}

/* Parse the body of a loop delimited by do/done. */
static Command *parse_loop_body(char **p) {
    char *body = gather_until_done(p);
    if (!body)
        return NULL;
    Command *body_cmd = parse_line(body);
    free(body);
    return body_cmd;
}

/* Parse a while/until clause body using UNTIL flag for until loops. */
static Command *parse_loop_clause(char **p, int until) {
    const char *stop1[] = {"do"};
    char *cond = gather_until(p, stop1, 1, NULL);
    if (!cond)
        return NULL;
    Command *cond_cmd = parse_line(cond);
    free(cond);

    Command *body_cmd = parse_loop_body(p);
    if (!body_cmd) {
        free_commands(cond_cmd);
        return NULL;
    }

    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free_commands(cond_cmd);
        free_commands(body_cmd);
        return NULL;
    }

    cmd->type = until ? CMD_UNTIL : CMD_WHILE;
    cmd->cond = cond_cmd;
    cmd->body = body_cmd;
    return cmd;
}

/* Parse a while loop starting at *p. */
static Command *parse_while_clause(char **p) {
    return parse_loop_clause(p, 0);
}

/* Parse an until loop starting at *p. */
static Command *parse_until_clause(char **p) {
    return parse_loop_clause(p, 1);
}

/* Collect a list of words for for/select loops. */
static int parse_word_list(char **p, char ***out, int **quoted_out,
                          int **expand_out, int *count) {
    CLEANUP_STRARRAY StrArray arr;
    strarray_init(&arr);
    int cap = 0;
    CLEANUP_FREE int *qflags = NULL;
    CLEANUP_FREE int *eflags = NULL;
    while (1) {
        int q = 0; int de = 1;
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == ';') {
            (*p)++;
            while (**p == ' ' || **p == '\t') (*p)++;
            char *next = read_token(p, &q, &de);
            if (!next) {
                strarray_release(&arr);
                return -1;
            }
            if (!q && strcmp(next, "do") == 0) { free(next); break; }
            char *dup_tok = next;
            if (strarray_push(&arr, dup_tok) == -1) {
                free(dup_tok);
                goto fail;
            }
            if (arr.count > cap) {
                int nc = cap ? cap * 2 : 4;
                int *tmpq = realloc(qflags, nc * sizeof(int));
                int *tmpe = realloc(eflags, nc * sizeof(int));
                if (!tmpq || !tmpe) { free(tmpq); free(tmpe); goto fail; }
                qflags = tmpq; eflags = tmpe; cap = nc;
            }
            qflags[arr.count - 1] = q;
            eflags[arr.count - 1] = de;
            continue;
        }
        if (**p == '\0') {
            goto fail;
        }
        q = 0; de = 1;
        char *w = read_token(p, &q, &de);
        if (!w) {
            goto fail;
        }
        if (!q && strcmp(w, "do") == 0) { free(w); break; }
        if (!q && strcmp(w, ";") == 0) {
            free(w);
            while (**p == ' ' || **p == '\t') (*p)++;
            q = 0; de = 1;
            char *next = read_token(p, &q, &de);
            if (!next) {
                strarray_release(&arr);
                return -1;
            }
            if (!q && strcmp(next, "do") == 0) { free(next); break; }
            if (strarray_push(&arr, next) == -1) {
                free(next);
                goto fail;
            }
            if (arr.count > cap) {
                int nc = cap ? cap * 2 : 4;
                int *tmpq = realloc(qflags, nc * sizeof(int));
                int *tmpe = realloc(eflags, nc * sizeof(int));
                if (!tmpq || !tmpe) { free(tmpq); free(tmpe); goto fail; }
                qflags = tmpq; eflags = tmpe; cap = nc;
            }
            qflags[arr.count - 1] = q;
            eflags[arr.count - 1] = de;
            continue;
        }
        if (strarray_push(&arr, w) == -1) {
            free(w);
            goto fail;
        }
        if (arr.count > cap) {
            int nc = cap ? cap * 2 : 4;
            int *tmpq = realloc(qflags, nc * sizeof(int));
            int *tmpe = realloc(eflags, nc * sizeof(int));
            if (!tmpq || !tmpe) { free(tmpq); free(tmpe); goto fail; }
            qflags = tmpq; eflags = tmpe; cap = nc;
        }
        qflags[arr.count - 1] = q;
        eflags[arr.count - 1] = de;
    }
    {
        int cnt = arr.count;
        char **vals = strarray_finish(&arr);
        if (!vals) goto fail;
        *out = vals;
        if (quoted_out) { *quoted_out = qflags; qflags = NULL; }
        if (expand_out) { *expand_out = eflags; eflags = NULL; }
        if (count) *count = cnt;
        return 0;
    }
fail:
    return -1;
}

/* Parse a traditional for loop clause. */
static Command *parse_for_clause(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    int q = 0; int de = 1;
    char *var = read_token(p, &q, &de);
    if (!var || q) { free(var); return NULL; }
    while (**p == ' ' || **p == '\t') (*p)++;
    q = 0; de = 1;
    char *tok = read_token(p, &q, &de);
    if (!tok || strcmp(tok, "in") != 0) { free(var); free(tok); return NULL; }
    free(tok);
    char **words = NULL; int count = 0; int *qflags = NULL; int *eflags = NULL;
    if (parse_word_list(p, &words, &qflags, &eflags, &count) == -1) {
        free(var);
        return NULL;
    }
    Command *body_cmd = parse_loop_body(p);
    if (!body_cmd) {
        free(var);
        for (int i=0;i<count;i++)
            free(words[i]);
        free(words);
        return NULL;
    }
    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free(var);
        for (int i=0; i<count; i++)
            free(words[i]);
        free(words);
        free_commands(body_cmd);
        return NULL;
    }
    cmd->type = CMD_FOR;
    cmd->var = var;
    cmd->words = words;
    cmd->word_count = count;
    cmd->word_quoted = qflags;
    cmd->word_expand = eflags;
    cmd->body = body_cmd;
    return cmd;
}

/* Parse a select loop clause. */
static Command *parse_select_clause(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    int q = 0; int de = 1;
    char *var = read_token(p, &q, &de);
    if (!var || q) { free(var); return NULL; }
    while (**p == ' ' || **p == '\t') (*p)++;
    q = 0; de = 1;
    char *tok = read_token(p, &q, &de);
    if (!tok || strcmp(tok, "in") != 0) { free(var); free(tok); return NULL; }
    free(tok);
    char **words = NULL; int count = 0; int *qflags = NULL; int *eflags = NULL;
    if (parse_word_list(p, &words, &qflags, &eflags, &count) == -1) {
        free(var);
        return NULL;
    }
    Command *body_cmd = parse_loop_body(p);
    if (!body_cmd) {
        free(var);
        for (int i=0;i<count;i++)
            free(words[i]);
        free(words);
        return NULL;
    }
    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free(var);
        for (int i=0;i<count;i++)
            free(words[i]);
        free(words);
        free_commands(body_cmd);
        return NULL;
    }
    cmd->type = CMD_SELECT;
    cmd->var = var;
    cmd->words = words;
    cmd->word_count = count;
    cmd->word_quoted = qflags;
    cmd->word_expand = eflags;
    cmd->body = body_cmd;
    return cmd;
}

/* Split arithmetic for loop expressions into init, cond and incr parts. */
static char *parse_for_arith_exprs(char **p, char **init, char **cond, char **incr) {
    while (**p == ' ' || **p == '\t') (*p)++;
    char *exprs = gather_dbl_parens(p);
    if (!exprs)
        return NULL;
    char *s1 = strchr(exprs, ';');
    if (!s1) {
        free(exprs);
        return NULL;
    }
    char *s2 = strchr(s1 + 1, ';');
    if (!s2) {
        free(exprs);
        return NULL;
    }

    *init = strndup(exprs, s1 - exprs);
    if (!*init) {
        free(exprs);
        return NULL;
    }
    *cond = strndup(s1 + 1, s2 - (s1 + 1));
    if (!*cond) {
        free(exprs);
        free(*init);
        return NULL;
    }
    *incr = strdup(s2 + 1);
    if (!*incr) {
        free(exprs);
        free(*init);
        free(*cond);
        return NULL;
    }

    free(exprs);

    char *ti = trim_ws(*init);
    char *tc = trim_ws(*cond);
    char *tu = trim_ws(*incr);
    free(*init);
    free(*cond);
    free(*incr);
    *init = ti;
    *cond = tc;
    *incr = tu;
    return *init;
}

/* Parse arithmetic for ((init; cond; incr)) clause. */
static Command *parse_for_arith_clause(char **p) {
    char *init, *cond, *incr;
    if (!parse_for_arith_exprs(p, &init, &cond, &incr))
        return NULL;
    while (**p == ' ' || **p == '\t' || **p == '\n') (*p)++;
    if (**p == ';') {
        (*p)++;
        while (**p == ' ' || **p == '\t' || **p == '\n') (*p)++;
    }
    int q = 0; int de = 1; char *tok = read_token(p, &q, &de);
    if (!tok || strcmp(tok, "do") != 0) { free(init); free(cond); free(incr); free(tok); return NULL; }
    free(tok);
    Command *body_cmd = parse_loop_body(p);
    if (!body_cmd) {
        free(init);
        free(cond);
        free(incr);
        return NULL;
    }
    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free(init);
        free(cond);
        free(incr);
        free_commands(body_cmd);
        return NULL;
    }
    cmd->type = CMD_FOR_ARITH;
    cmd->arith_init = init;
    cmd->arith_cond = cond;
    cmd->arith_update = incr;
    cmd->body = body_cmd;
    return cmd;
}

/* Free the linked list of case patterns and their command bodies. */
void free_case_items(CaseItem *ci) {
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

/* Parse a single case item (patterns and body). */
static CaseItem *parse_case_item(char **p) {
    CLEANUP_STRARRAY StrArray patarr;
    strarray_init(&patarr);
    int done = 0;
    while (!done) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (**p == '(') { (*p)++; continue; }
        int q = 0; int de = 1;
        char *ptok = read_token(p, &q, &de);
        if (!ptok) {
            return NULL;
        }
        if (!q && strcmp(ptok, "|") == 0) { free(ptok); continue; }
        if (!q && strcmp(ptok, ")") == 0) { free(ptok); break; }
        size_t len = strlen(ptok);
        if (!q && len > 0 && ptok[len-1] == ')') { ptok[len-1] = '\0'; done = 1; }
        if (strarray_push(&patarr, ptok) == -1) {
            free(ptok);
            return NULL;
        }
        if (done) break;
    }

    const char *stops[] = {";;", ";&"};
    int idx = -1;
    char *body = gather_until(p, stops, 2, &idx);
    if (!body) {
        return NULL;
    }
    if (idx == 1 && opt_posix) {
        fprintf(stderr, "syntax error: ';&' not allowed in posix mode\n");
        free(body);
        return NULL;
    }
    Command *body_cmd = parse_line(body);
    free(body);
    CaseItem *ci = xcalloc(1, sizeof(CaseItem));
    if (!ci) {
        free_commands(body_cmd);
        return NULL;
    }
    int pat_count = patarr.count;
    ci->patterns = strarray_finish(&patarr);
    if (!ci->patterns) {
        free_commands(body_cmd);
        free(ci);
        return NULL;
    }
    ci->pattern_count = pat_count;
    ci->body = body_cmd;
    ci->fall_through = (idx == 1);
    return ci;
}

/* Parse a case/esac clause starting at *p. */
static Command *parse_case_clause(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    int q = 0; int de = 1;
    char *word = read_token(p, &q, &de);
    if (!word) return NULL;
    while (**p == ' ' || **p == '\t') (*p)++;
    q = 0; de = 1;
    char *tok = read_token(p, &q, &de);
    if (!tok || strcmp(tok, "in") != 0) { free(word); free(tok); return NULL; }
    free(tok);

    CaseItem *head = NULL, *tail = NULL;
    while (1) {
        while (**p == ' ' || **p == '\t' || **p == '\n') (*p)++;
        if (strncmp(*p, "esac", 4) == 0) { *p += 4; break; }

        CaseItem *ci = parse_case_item(p);
        if (!ci) { free_case_items(head); free(word); return NULL; }
        if (!head) head = ci; else tail->next = ci; tail = ci;
    }

    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free(word);
        free_case_items(head);
        return NULL;
    }
    cmd->type = CMD_CASE;
    cmd->var = word;
    cmd->cases = head;
    return cmd;
}

/* Parse a shell function definition and return the command node. */
Command *parse_function_def(char **p, CmdOp *op_out) {
    char *savep = *p;
    int qfunc = 0; int de = 1;
    char *tok = read_token(p, &qfunc, &de);
    if (!tok)
        return NULL;

    int using_kw = 0;
    if (!qfunc && strcmp(tok, "function") == 0 && (**p == ' ' || **p == '\t')) {
        if (opt_posix) {
            *p = savep;
            free(tok);
            return NULL;
        }
        using_kw = 1;
        while (**p == ' ' || **p == '\t') (*p)++;
        free(tok);
        tok = read_token(p, &qfunc, &de);
        if (!tok || qfunc) { goto fail; }
    }

    char *fname = tok;
    if (!qfunc) {
        size_t len = strlen(fname);
        if (len > 2 && fname[len-2] == '(' && fname[len-1] == ')') {
            fname[len-2] = '\0';
        } else if (**p == '(' && *(*p + 1) == ')') {
            *p += 2;
        } else if (!using_kw) {
            goto fail;
        }
    } else {
        goto fail;
    }

    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '{') {
        char *bodytxt = gather_braced(p);
        if (!bodytxt) goto fail;
        Command *body_cmd = NULL;
        Command *cmd = xcalloc(1, sizeof(Command));
        if (!cmd) {
            free(tok);
            free(bodytxt);
            free_commands(body_cmd);
            return NULL;
        }
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

fail:
    *p = savep;
    free(tok);
    return NULL;
}

/* Parse a subshell command enclosed in parentheses. */
Command *parse_subshell(char **p, CmdOp *op_out) {
    char *bodytxt = gather_parens(p);
    if (!bodytxt)
        return NULL;
    Command *body_cmd = parse_line(bodytxt);
    free(bodytxt);
    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free_commands(body_cmd);
        return NULL;
    }
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

/* Parse a command group enclosed in braces. */
Command *parse_brace_group(char **p, CmdOp *op_out) {
    char *bodytxt = gather_braced(p);
    if (!bodytxt)
        return NULL;
    Command *body_cmd = parse_line(bodytxt);
    free(bodytxt);
    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free_commands(body_cmd);
        return NULL;
    }
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

/* Parse a [[ conditional ]] expression. */
Command *parse_conditional(char **p, CmdOp *op_out) {
    if (strncmp(*p, "[[", 2) != 0)
        return NULL;
    if (opt_posix)
        return NULL;
    *p += 2;
    StrArray arr;
    strarray_init(&arr);
    while (**p) {
        while (**p == ' ' || **p == '\t') (*p)++;
        if (!**p)
            break;
        int q = 0; int de = 1;
        char *tok = read_token(p, &q, &de);
        if (!tok) {
            strarray_release(&arr);
            return NULL;
        }
        if (!q && strcmp(tok, "]]") == 0) {
            free(tok);
            break;
        }
        if (strarray_push(&arr, tok) == -1) {
            free(tok);
            strarray_release(&arr);
            return NULL;
        }
    }
    if (!arr.items && arr.count==0) {
        /* ensure an empty argument list is well-formed */
        arr.items = xcalloc(1, sizeof(char*));
        if (!arr.items)
            return NULL;
        arr.capacity = 1;
    } else {
        char **tmp = realloc(arr.items, sizeof(char*) * (arr.count + 1));
        if (!tmp) {
            strarray_release(&arr);
            return NULL;
        }
        arr.items = tmp;
    }
    arr.items[arr.count] = NULL;
    while (**p == ' ' || **p == '\t') (*p)++;
    CmdOp op = OP_NONE;
    if (**p == ';') { op = OP_SEMI; (*p)++; }
    else if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; }
    else if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; }
    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        strarray_free(arr.items);
        return NULL;
    }
    cmd->type = CMD_COND;
    cmd->words = arr.items;
    cmd->word_count = arr.count;
    cmd->op = op;
    if (op_out) *op_out = op;
    return cmd;
}

/* Parse a $(( expression )) arithmetic command. */
Command *parse_arith_command(char **p, CmdOp *op_out) {
    char *expr = gather_dbl_parens(p);
    if (!expr)
        return NULL;
    char *trim = trim_ws(expr);
    free(expr);

    while (**p == ' ' || **p == '\t') (*p)++;
    CmdOp op = OP_NONE;
    if (**p == ';') { op = OP_SEMI; (*p)++; }
    else if (**p == '&' && *(*p + 1) == '&') { op = OP_AND; (*p) += 2; }
    else if (**p == '|' && *(*p + 1) == '|') { op = OP_OR; (*p) += 2; }

    Command *cmd = xcalloc(1, sizeof(Command));
    if (!cmd) {
        free(trim);
        return NULL;
    }
    cmd->type = CMD_ARITH;
    cmd->text = trim;
    cmd->op = op;
    if (op_out) *op_out = op;
    return cmd;
}

/* Dispatch to the appropriate control clause parser based on keywords. */
Command *parse_control_clause(char **p, CmdOp *op_out) {
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
        while (**p == ' ' || **p == '\t') (*p)++;
        if (strncmp(*p, "((", 2) == 0)
            cmd = parse_for_arith_clause(p);
        else
            cmd = parse_for_clause(p);
    } else if (strncmp(*p, "select", 6) == 0 && isspace((unsigned char)(*p)[6])) {
        *p += 6;
        cmd = parse_select_clause(p);
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

