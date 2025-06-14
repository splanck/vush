#define _GNU_SOURCE
#include "lexer.h"
#include "parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration from lexer_expand.c */
char *parse_substitution(char **p);
static char *parse_quoted_word(char **p, int *quoted, int *do_expand_out);

/* set when a memory allocation fails inside parse_redirect_token */
static int token_alloc_failed = 0;

/* If *p points at a redirection or control operator, extract the operator
 * token and advance *p.  Returns the allocated token string or NULL when
 * no operator is found. */
static char *parse_redirect_token(char **p) {
    char buf[MAX_LINE];
    int len = 0;
    token_alloc_failed = 0;
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
        char *tmp = strdup(buf);
        if (!tmp) {
            token_alloc_failed = 1;
            return NULL;
        }
        return tmp;
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
        char *tmp = strdup(buf);
        if (!tmp) {
            token_alloc_failed = 1;
            return NULL;
        }
        return tmp;
    }
    if (**p == '>' && *(*p + 1) == '|') {
        buf[len++] = '>';
        (*p)++;
        buf[len++] = '|';
        (*p)++;
        buf[len] = '\0';
        char *tmp = strdup(buf);
        if (!tmp) {
            token_alloc_failed = 1;
            return NULL;
        }
        return tmp;
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
        char *tmp = strdup(buf);
        if (!tmp) {
            token_alloc_failed = 1;
            return NULL;
        }
        return tmp;
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
        char *tmp = strdup(buf);
        if (!tmp) {
            token_alloc_failed = 1;
            return NULL;
        }
        return tmp;
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
        char *tmp = strdup(buf);
        if (!tmp) {
            token_alloc_failed = 1;
            return NULL;
        }
        return tmp;
    }
    return NULL;
}

/* Consume a backslash escape from *p and append the escaped character to BUF.
 * FIRST indicates whether this is the first character of the token and thus
 * controls whether variable expansion should occur. DO_EXPAND is updated
 * accordingly. */
static void handle_backslash_escape(char **p, char buf[], int *len,
                                   int *first, int *do_expand,
                                   int disable_first) {
    (*p)++;
    if (**p) {
        if (*len < MAX_LINE - 1)
            buf[(*len)++] = **p;
        if (*first && disable_first && (**p == '$' || **p == '`'))
            *do_expand = 0;
        if (**p)
            (*p)++;
    }
    *first = 0;
}


/* Return non-zero when character C terminates an unquoted token. */
static int is_end_unquoted(int c) {
    return c == ' ' || c == '\t' || c == '|' || c == '<' ||
           c == '>' || c == '&' || c == ';';
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
    while (**p && !is_end((unsigned char)**p)) {
        if (**p == '$' && *(*p + 1) == '(' && *(*p + 2) == '(') {
            int depth = 1;
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
            while (**p && !(**p == ')' && depth == 0 && *(*p + 1) == ')')) {
                if (**p == '(') depth++;
                else if (**p == ')') depth--;
                if (*len < MAX_LINE - 1) buf[(*len)++] = **p;
                (*p)++;
            }
            if (!**p) {
                parse_need_more = 1;
                return -1;
            } else if (**p == ')' && *(*p + 1) == ')' && depth == 0) {
                if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
                if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
            } else {
                fprintf(stderr, "syntax error: unmatched ')'\n");
                return -1;
            }
            first = 0;
            continue;
        }
        if (**p == '\'' || **p == '"') {
            int q = 0; int de = 1;
            char *part = parse_quoted_word(p, &q, &de);
            if (!part)
                return -1;
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
                parse_need_more = 1;
                return -1;
            }
            if (startc == '`') {
                if (**p == '`') {
                    if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
                    closed = 1;
                } else if (!closed) {
                    parse_need_more = 1;
                    return -1;
                }
            }
            first = 0;
            continue;
        }
        if (**p == '\\') {
            handle_backslash_escape(p, buf, len, &first, do_expand,
                                   disable_first);
            continue;
        }
        if (**p == '$' && *(*p + 1) == '{') {
            const char *start = *p;
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++; /* $ */
            if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++; /* { */
            while (**p && **p != '}' && *len < MAX_LINE - 1) {
                buf[(*len)++] = **p;
                (*p)++;
            }
            if (**p == '}') {
                if (*len < MAX_LINE - 1) buf[(*len)++] = *(*p)++;
                first = 0;
                continue;
            }
            /* unmatched, reset pointer for error */
            *p = (char *)start;
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
            parse_need_more = 1;
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
            parse_need_more = 1;
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

/* Read the next shell token from *p performing necessary expansions.
 * QUOTED is set when the token was quoted.  The returned string is
 * dynamically allocated and *p is advanced past the token. */
char *read_token(char **p, int *quoted, int *do_expand_out) {
    char buf[MAX_LINE];
    int len = 0;
    int do_expand = parse_noexpand ? 0 : 1;
    *quoted = 0;
    char *redir = parse_redirect_token(p);
    if (token_alloc_failed)
        return NULL;
    if (redir)
        return redir;
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

