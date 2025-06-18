#define _GNU_SOURCE
#include "lexer.h"
#include "var_expand.h"
#include "history.h"
#include "builtins.h"
#include "vars.h"
#include "scriptargs.h"
#include "options.h"
#include "jobs.h"
#include "arith.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <ctype.h>
#include <pwd.h>
#include <unistd.h>
#include "parser.h" /* for MAX_LINE */
#include "execute.h"
#include "util.h"
#include "cmd_subst.h"

extern int last_status;

/* Append STR to OUT reallocating as needed. Returns 0 on allocation failure. */
static int append_str(char **out, size_t *len, const char *str) {
    size_t slen = strlen(str);
    char *tmp = realloc(*out, *len + slen + 1);
    if (!tmp)
        return 0;
    *out = tmp;
    memcpy(*out + *len, str, slen);
    *len += slen;
    (*out)[*len] = '\0';
    return 1;
}

/* Try to expand a command substitution at *P and append the result to OUT. */
static int handle_cmd_sub(const char **p, char **out, size_t *outlen) {
    const char *s = *p;
    if (*s == '`' || (*s == '$' && s[1] == '(' && s[2] != '(')) {
        char *dup = strdup(s);
        if (!dup)
            return -1;
        char *dp = dup;
        char *sub = parse_substitution(&dp);
        if (sub && dp > dup) {
            size_t consumed = (size_t)(dp - dup);
            *p += consumed;
            if (!append_str(out, outlen, sub)) {
                free(sub);
                free(dup);
                return -1;
            }
            free(sub);
            free(dup);
            return 1;
        }
        free(dup);
    }
    return 0;
}

/* Try to expand an arithmetic expression starting at *P.  The expression
 * is captured using gather_dbl_parens() so nested parentheses are handled
 * correctly. */
static int handle_arith(const char **p, char **out, size_t *outlen) {
    const char *s = *p;
    if (*s == '$' && s[1] == '(' && s[2] == '(') {
        char *dup = strdup(s + 1); /* after '$' */
        if (dup) {
            char *dp = dup;
            char *body = gather_dbl_parens(&dp);
            if (body) {
                size_t consumed = (size_t)(dp - dup) + 1; /* include '$' */
                char buf[MAX_LINE];
                if (consumed >= sizeof(buf))
                    consumed = sizeof(buf) - 1;
                memcpy(buf, s, consumed);
                buf[consumed] = '\0';
                char *exp = expand_simple(buf);
                if (!exp || !append_str(out, outlen, exp)) {
                    free(exp);
                    free(body);
                    free(dup);
                    return -1;
                }
                free(exp);
                free(body);
                *p += consumed;
                free(dup);
                return 1;
            }
            free(dup);
        }
    }
    return 0;
}

/* Try to expand a parameter reference at *P. */
static int handle_param(const char **p, char **out, size_t *outlen) {
    const char *s = *p;
    if (*s != '$')
        return 0;

    const char *start = s;
    if (s[1] == '{') {
        const char *end = strchr(s + 2, '}');
        if (end) {
            size_t len = (size_t)(end - start + 1);
            char buf[MAX_LINE];
            if (len >= sizeof(buf))
                len = sizeof(buf) - 1;
            memcpy(buf, start, len);
            buf[len] = '\0';
            char *exp = expand_simple(buf);
            if (!exp || !append_str(out, outlen, exp)) {
                free(exp);
                return -1;
            }
            free(exp);
            *p += len;
            return 1;
        }
    } else {
        const char *q = s + 1;
        if (*q == '#' || *q == '?' || *q == '*' || *q == '@' ||
            *q == '$' || *q == '!' || *q == '-') {
            q++;
        } else if (isdigit((unsigned char)*q)) {
            q++;
            while (isdigit((unsigned char)*q))
                q++;
        } else {
            while (*q && (isalnum((unsigned char)*q) || *q == '_'))
                q++;
        }
        if (q > s + 1) {
            size_t len = (size_t)(q - start);
            char buf[MAX_LINE];
            if (len >= sizeof(buf))
                len = sizeof(buf) - 1;
            memcpy(buf, start, len);
            buf[len] = '\0';
            char *exp = expand_simple(buf);
            if (!exp || !append_str(out, outlen, exp)) {
                free(exp);
                return -1;
            }
            free(exp);
            *p += len;
            return 1;
        }
    }
    return 0;
}

/* Expand TOKEN which may contain multiple variable or command substitutions. */
char *expand_var(const char *token) {
    if (!token)
        return strdup("");

    /* Fast path for simple tokens without any expansions. */
    if (!strchr(token, '$') && !strchr(token, '`')) {
        if (token[0] == '~')
            return expand_simple(token);
        return strdup(token);
    }

    /* If the token is wrapped in double quotes, remove them before
     * processing so that expansions inside quoted strings don't
     * preserve the quote characters. */
    size_t tlen = strlen(token);
    if (tlen >= 3 && token[0] == '$' && token[1] == '\'' && token[tlen - 1] == '\'') {
        size_t innerlen = tlen - 3;
        if (innerlen >= MAX_LINE)
            innerlen = MAX_LINE - 1;
        char inner[MAX_LINE];
        memcpy(inner, token + 2, innerlen);
        inner[innerlen] = '\0';
        return ansi_unescape(inner);
    }
    if (tlen >= 2 && token[0] == '\'' && token[tlen - 1] == '\'') {
        return strndup(token + 1, tlen - 2);
    }
    if (tlen >= 2 && token[0] == '"' && token[tlen - 1] == '"') {
        size_t innerlen = tlen - 2;
        if (innerlen >= MAX_LINE)
            innerlen = MAX_LINE - 1;
        char inner[MAX_LINE];
        memcpy(inner, token + 1, innerlen);
        inner[innerlen] = '\0';
        char *res = expand_var(inner);
        if (!res)
            return NULL;
        char *quoted = malloc(strlen(res) + 3);
        if (!quoted) {
            free(res);
            return NULL;
        }
        quoted[0] = '"';
        strcpy(quoted + 1, res);
        size_t rlen = strlen(res);
        quoted[rlen + 1] = '"';
        quoted[rlen + 2] = '\0';
        free(res);
        return quoted;
    }

    char *out = xcalloc(1, 1);
    size_t outlen = 0;

    const char *p = token;
    while (*p) {
        int r = handle_cmd_sub(&p, &out, &outlen);
        if (r < 0) { free(out); return NULL; }
        if (r > 0) continue;

        r = handle_arith(&p, &out, &outlen);
        if (r < 0) { free(out); return NULL; }
        if (r > 0) continue;

        r = handle_param(&p, &out, &outlen);
        if (r < 0) { free(out); return NULL; }
        if (r > 0) continue;

        if (*p == '$') {
            if (!append_str(&out, &outlen, "$")) { free(out); return NULL; }
            p++;
            continue;
        }

        char ch[2] = {*p, '\0'};
        if (!append_str(&out, &outlen, ch)) { free(out); return NULL; }
        p++;
    }

    return out;
}

/* Perform history expansion on LINE when it begins with '!'. Returns a new
 * string with the expansion applied or NULL on error. */
