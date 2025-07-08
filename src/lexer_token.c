/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Tokenization routines.
 */

#define _GNU_SOURCE
#include "lexer.h"
#include "parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmd_subst.h"
static char *parse_quoted_word(char **p, int *quoted, int *do_expand_out);
static char *parse_ansi_quoted_word(char **p, int *quoted, int *do_expand_out);

/* set when a memory allocation fails inside read_redirect_token */
static int token_alloc_failed = 0;

/* Allocate and return a copy of BUF. Sets token_alloc_failed on failure. */
static char *dup_token_string(const char buf[]) {
    char *tmp = strdup(buf);
    if (!tmp)
        token_alloc_failed = 1;
    return tmp;
}

/* Handle patterns like '2>' or '&>' optionally followed by another '>'. */
static char *read_fd_redirect(char **p) {
    if ((**p != '2' && **p != '&') || *(*p + 1) != '>')
        return NULL;
    char buf[MAX_LINE];
    int len = 0;
    buf[len++] = **p;
    (*p)++;
    buf[len++] = '>';
    (*p)++;
    if (**p == '>') {
        buf[len++] = '>';
        (*p)++;
    }
    buf[len] = '\0';
    return dup_token_string(buf);
}

/* If *p points at a redirection or control operator, extract the operator
 * token and advance *p.  Returns the allocated token string or NULL when
 * no operator is found. */
static char *read_redirect_token(char **p) {
    char buf[MAX_LINE];
    int len = 0;
    token_alloc_failed = 0;
    char *redir = read_fd_redirect(p);
    if (redir)
        return redir;
    if (**p == '>' && *(*p + 1) == '|') {
        buf[len++] = '>';
        (*p)++;
        buf[len++] = '|';
        (*p)++;
        buf[len] = '\0';
        return dup_token_string(buf);
    }
    if (**p == '&' && *(*p + 1) != '&' && *(*p + 1) != '>') {
        buf[len++] = '&';
        (*p)++;
        while (**p && **p != ' ' && **p != '\t' && **p != '|' && **p != '<' &&
               **p != '>' && **p != '&' && **p != ';' && len < MAX_LINE - 1) {
            buf[len++] = **p;
            (*p)++;
        }
        buf[len] = '\0';
        return dup_token_string(buf);
    }
    if (**p == '<' && *(*p + 1) == '<') {
        buf[len++] = '<';
        (*p)++;
        buf[len++] = '<';
        (*p)++;
        while (**p && **p != ' ' && **p != '\t' && **p != '|' && **p != '<' &&
               **p != '>' && **p != '&' && **p != ';' && len < MAX_LINE - 1) {
            buf[len++] = **p;
            (*p)++;
        }
        buf[len] = '\0';
        return dup_token_string(buf);
    }
    if (**p == '>' || **p == '<' || **p == '|' || **p == '&' || **p == ';') {
        buf[len++] = **p;
        (*p)++;
        if ((buf[0] == '>' && **p == '>') ||
            (buf[0] == '&' && **p == '&') ||
            (buf[0] == '|' && **p == '|') ||
            (buf[0] == ';' && (**p == ';' || **p == '&'))) {
            buf[len++] = **p;
            (*p)++;
        }
        buf[len] = '\0';
        return dup_token_string(buf);
    }
    return NULL;
}

/* Consume a backslash escape from *p and append the escaped character to BUF.
 * FIRST indicates whether this is the first character of the token and thus
 * controls whether variable expansion should occur. DO_EXPAND is updated
 * accordingly. */
static void read_backslash_escape(char **p, char buf[], int *len,
                                 int *first, int *do_expand,
                                 int disable_first) {
    (*p)++; /* move past the backslash */
    if (!**p) {
        /* dangling backslash at end of input */
        if (*len < MAX_LINE - 1)
            buf[(*len)++] = '\\';
        *first = 0;
        return;
    }

    if (!disable_first &&
        (**p == '$' || **p == '`' || **p == '"' || **p == '\\')) {
        /* within double quotes drop the backslash */
        if (*len < MAX_LINE - 1)
            buf[(*len)++] = **p;
        (*p)++;
        *first = 0;
        return;
    }

    /* default behavior: preserve backslash */
    if (*len < MAX_LINE - 1)
        buf[(*len)++] = '\\';
    if (*len < MAX_LINE - 1)
        buf[(*len)++] = **p;
    if (*first && disable_first && (**p == '$' || **p == '`'))
        *do_expand = 0;
    (*p)++;
    *first = 0;
}

/* Attempt to read a ${...} braced expansion. Returns 1 when consumed. */
static int read_braced_expansion(char **p, char buf[], int *len) {
    const char *start = *p;
    if (!(**p == '$' && *(*p + 1) == '{'))
        return 0;

    if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++; /* $ */
    if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++; /* { */
    while (**p && **p != '}' && *len < MAX_LINE - 1) {
        buf[(*len)++] = **p;
        (*p)++;
    }
    if (**p == '}') {
        if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
        return 1;
    }
    /* unmatched, reset pointer for error */
    *p = (char *)start;
    return 0;
}


/* Return non-zero when character C terminates an unquoted token. */
static int is_end_unquoted(int c) {
    return c == ' ' || c == '\t' || c == '|' || c == '<' ||
           c == '>' || c == '&' || c == ';' || c == '\r' || c == '\n';
}

/* Return non-zero when character C terminates a double quoted token. */
static int is_end_dquote(int c) {
    return c == '"';
}

/* Read characters for a simple token using IS_END as the terminator predicate.
 * Characters are copied into BUF while tracking quoting state.  No variable
 * or command substitution is performed here so that expansion can be delayed
 * until execution time.  DO_EXPAND is set to zero when quoting prevents
 * expansion.  Returns 0 on success or -1 on errors. */
static int read_simple_token(char **p, int (*is_end)(int), char buf[],
                             int *len, int *do_expand, int disable_first) {
    int first = 1;
    int in_assign = 0;

    /* Fast path for arithmetic expansion starting a token so that the
     * expression is returned as a single unit. */
    if (**p == '$' && *(*p + 1) == '(' && *(*p + 2) == '(') {
        char *dp = *p + 1; /* skip '$' for gather_dbl_parens */
        char *body = gather_dbl_parens(&dp);
        if (!body) {
            parse_need_more = 1;
            return -1;
        }
        if (*len < MAX_LINE - 1)
            buf[(*len)++] = '$';
        size_t l = (size_t)(dp - (*p + 1)); /* length of "((expr))" */
        size_t avail = (size_t)(MAX_LINE - 1 - *len);
        if (l > avail)
            l = avail;
        memcpy(buf + *len, *p + 1, l);
        *len += l;
        *p = dp;
        free(body);
        return 0;
    }

    while (**p && !is_end((unsigned char)**p)) {
        if (!in_assign && **p == '=') {
            if (*len < MAX_LINE - 1)
                buf[(*len)++] = **p;
            (*p)++;
            in_assign = 1;
            first = 0;
            continue;
        }
        if (**p == '$' && *(*p + 1) == '(' && *(*p + 2) == '(') {
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++; /* '$' */
            char *dp = *p; /* points at "((" */
            char *body = gather_dbl_parens(&dp);
            if (!body) {
                parse_need_more = 1;
                return -1;
            }
            size_t l = (size_t)(dp - *p); /* length of "((expr))" */
            size_t avail = (size_t)(MAX_LINE - 1 - *len);
            if (l > avail)
                l = avail;
            memcpy(buf + *len, *p, l);
            *len += l;
            *p = dp;
            free(body);
            first = 0;
            continue;
        }
        if (**p == '$' && *(*p + 1) == '\'') {
            int q = 0; int de = 0;
            char *part = parse_ansi_quoted_word(p, &q, &de);
            if (!part)
                return -1;
            for (int ci = 0; part[ci] && *len < MAX_LINE - 1; ci++)
                buf[(*len)++] = part[ci];
            free(part);
            *do_expand = 0;
            first = 0;
            continue;
        }
        if (**p == '\'' || **p == '"') {
            char quote = **p;
            int q = 0; int de = 1;
            char *part = parse_quoted_word(p, &q, &de);
            if (!part)
                return -1;
            if (quote == '\'' && in_assign && *len > 0 && buf[*len - 1] != '=') {
                if (*len < MAX_LINE - 1)
                    buf[(*len)++] = '\'';
            }
            for (int ci = 0; part[ci] && *len < MAX_LINE - 1; ci++)
                buf[(*len)++] = part[ci];
            free(part);
            *do_expand = 0;
            first = 0;
            continue;
        }
        if (**p == '`' || (**p == '$' && *(*p + 1) == '(')) {
            int depth = 0;
            int closed = 0;
            char startc = **p;
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
            if (startc == '$') {
                if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++; /* '(' */
                depth = 1;
            }
            while (**p && ((startc == '`' && **p != '`') ||
                          (startc == '$' && depth > 0))) {
                if (startc == '$') {
                    if (**p == '(') depth++;
                    else if (**p == ')') {
                        depth--;
                        if (depth == 0) {
                            if (*len < MAX_LINE - 1) buf[(*len)++] = **p;
                            (*p)++;
                            closed = 1;
                            break;
                        }
                    }
                }
                if (*len < MAX_LINE - 1) buf[(*len)++] = **p;
                (*p)++;
            }
            if (!closed && !**p) {
                fprintf(stderr, "syntax error: unmatched ')'\n");
                parse_need_more = 0;
                return -1;
            }
            if (startc == '`') {
                if (**p == '`') {
                    if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
                    closed = 1;
                } else if (!closed) {
                    fprintf(stderr, "syntax error: unmatched '`'\n");
                    parse_need_more = 0;
                    return -1;
                }
            }
            first = 0;
            continue;
        }
        if (**p == '\\') {
            if (disable_first && *(*p + 1) == '"') {
                /* Treat \"...\" in unquoted context as a quoted segment but
                 * preserve the quote characters. */
                char *start = *p + 2; /* skip opening \" */
                char *end = strstr(start, "\\\"");
                if (!end) {
                    fprintf(stderr, "syntax error: unmatched '\"'\n");
                    parse_need_more = 0;
                    return -1;
                }
                char tmp[MAX_LINE];
                size_t seglen = (size_t)(end - start);
                if (seglen > MAX_LINE - 3)
                    seglen = MAX_LINE - 3;
                tmp[0] = '"';
                memcpy(tmp + 1, start, seglen);
                tmp[1 + seglen] = '"';
                tmp[2 + seglen] = '\0';
                char *tp = tmp;
                int q = 0; int de = 1;
                char *part = parse_quoted_word(&tp, &q, &de);
                if (!part)
                    return -1;
                if (*len < MAX_LINE - 1)
                    buf[(*len)++] = '"';
                for (int ci = 0; part[ci] && *len < MAX_LINE - 1; ci++)
                    buf[(*len)++] = part[ci];
                if (*len < MAX_LINE - 1)
                    buf[(*len)++] = '"';
                free(part);
                *p = end + 2; /* skip closing \" */
                *do_expand = de;
                first = 0;
                continue;
            }
            read_backslash_escape(p, buf, len, &first, do_expand,
                                 disable_first);
            continue;
        }
        if (read_braced_expansion(p, buf, len)) {
            first = 0;
            continue;
        }
        if (**p == '$' && strchr("#?*@-$!", *(*p + 1))) {
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++; /* '$' */
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
            first = 0;
            continue;
        }
        if (**p == '$' && (isalnum((unsigned char)*(*p + 1)))) {
            if (*len < MAX_LINE - 1)
                buf[(*len)++] = *(*p)++; /* '$' */
            while (**p && isalnum((unsigned char)**p) && *len < MAX_LINE - 1) {
                buf[(*len)++] = **p;
                (*p)++;
            }
            first = 0;
            continue;
        }
        if (*len < MAX_LINE - 1) buf[(*len)++] = **p;
        (*p)++;
        first = 0;
    }
    return 0;
}

/* Parse a single or double quoted word starting at *p.  The QUOTED flag is
 * set and DO_EXPAND_OUT receives whether expansion should occur.  Returns the
 * resulting allocated string or NULL on syntax errors. */
static char *parse_quoted_word(char **p, int *quoted, int *do_expand_out) {
    char buf[MAX_LINE];
    int len = 0;
    int do_expand = 1;
    char quote = **p;
    *quoted = 1;
    if (quote == '\'') {
        do_expand = 0;
        (*p)++;
        while (**p && **p != quote && len < MAX_LINE - 1)
            buf[len++] = *(*p)++;
        if (**p == quote) {
            (*p)++;
        } else if (**p == '\0') {
            fprintf(stderr, "syntax error: unmatched '%c'\n", quote);
            parse_need_more = 0;
            return NULL;
        } else {
            fprintf(stderr, "syntax error: unmatched '%c'\n", quote);
            return NULL;
        }
    } else {
        (*p)++;
        if (read_simple_token(p, is_end_dquote, buf, &len, &do_expand,
                              0) < 0)
            return NULL;
        if (**p == quote) {
            (*p)++;
        } else if (**p == '\0') {
            fprintf(stderr, "syntax error: unmatched '\"'\n");
            parse_need_more = 0;
            return NULL;
        } else {
            fprintf(stderr, "syntax error: unmatched '\"'\n");
            return NULL;
        }
    }
    buf[len] = '\0';
    if (do_expand_out) *do_expand_out = do_expand;
    return strdup(buf);
}

/* Parse an ANSI-C quoted word starting at *p of the form $'...'.  QUOTED is
 * set and DO_EXPAND_OUT receives whether expansion should occur (always 0).
 * Returns the resulting allocated string or NULL on syntax errors. */
static char *parse_ansi_quoted_word(char **p, int *quoted, int *do_expand_out) {
    char buf[MAX_LINE];
    int len = 0;
    int do_expand = 0;
    *quoted = 1;
    *p += 2; /* skip $' */
    while (**p && **p != '\'' && len < MAX_LINE - 1) {
        if (**p == '\\' && (*p)[1]) {
            (*p)++;
            char c = **p;
            switch (c) {
            case 'n': buf[len++] = '\n'; break;
            case 't': buf[len++] = '\t'; break;
            case 'r': buf[len++] = '\r'; break;
            case 'b': buf[len++] = '\b'; break;
            case 'a': buf[len++] = '\a'; break;
            case 'f': buf[len++] = '\f'; break;
            case 'v': buf[len++] = '\v'; break;
            case '\\': buf[len++] = '\\'; break;
            case '\'': buf[len++] = '\''; break;
            case '"': buf[len++] = '"'; break;
            case '0': {
                int val = 0, cnt = 0;
                while (cnt < 3 && (*p)[1] >= '0' && (*p)[1] <= '7') {
                    (*p)++; cnt++; val = val * 8 + (**p - '0');
                }
                buf[len++] = (char)val;
                break;
            }
            default:
                buf[len++] = '\\';
                buf[len++] = c;
                break;
            }
        } else {
            buf[len++] = **p;
        }
        (*p)++;
    }
    if (**p == '\'') {
        (*p)++;
    } else if (**p == '\0') {
        fprintf(stderr, "syntax error: unmatched '\''\n");
        parse_need_more = 0;
        return NULL;
    } else {
        fprintf(stderr, "syntax error: unmatched '\''\n");
        return NULL;
    }
    buf[len] = '\0';
    if (do_expand_out) *do_expand_out = do_expand;
    return strdup(buf);
}

/* Read the next shell token from *p performing necessary expansions.
 * QUOTED is set when the token was quoted.  The returned string is
 * dynamically allocated and *p is advanced past the token. */
char *read_token(char **p, int *quoted, int *do_expand_out) {
    char buf[MAX_LINE];
    int len = 0;
    int do_expand = parse_noexpand ? 0 : 1;
    *quoted = 0;
    char *redir = read_redirect_token(p);
    if (token_alloc_failed)
        return NULL;
    if (redir)
        return redir;
    if (**p == '$' && *(*p + 1) == '\'') {
        char *res = parse_ansi_quoted_word(p, quoted, &do_expand);
        if (do_expand_out) *do_expand_out = do_expand;
        return res;
    }
    if (**p == '\'' || **p == '"') {
        char *res = parse_quoted_word(p, quoted, &do_expand);
        if (do_expand_out) *do_expand_out = do_expand;
        return res;
    }
    if (read_simple_token(p, is_end_unquoted, buf, &len, &do_expand,
                          1) < 0)
        return NULL;
    buf[len] = '\0';
    char *res = strdup(buf);
    if (getenv("VUSH_DEBUG"))
        fprintf(stderr, "read_token: '%s'\n", res);
    if (do_expand_out) *do_expand_out = do_expand;
    return res;
}

