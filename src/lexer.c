/* Lexical utilities and expansions */
#define _GNU_SOURCE
#include "lexer.h"
#include "history.h"
#include "builtins.h"
#include "scriptargs.h"
#include "options.h"
#include "arith.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <ctype.h>
#include <pwd.h>
#include <unistd.h>
#include "parser.h" /* for MAX_LINE */

extern int last_status;

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

static char *parse_substitution(char **p) {
    int depth = 0;
    int is_dollar = (**p == '$');
    (*p)++;
    if (is_dollar) {
        (*p)++;
        depth = 1;
    }
    char cmd[MAX_LINE];
    int clen = 0;
    while (**p && ((is_dollar && depth > 0) || (!is_dollar && **p != '`')) &&
           clen < MAX_LINE - 1) {
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
            (*p)++;
        else {
            fprintf(stderr, "syntax error: unmatched '`'\n");
            return NULL;
        }
    }
    cmd[clen] = '\0';
    return command_output(cmd);
}

static char *parse_redirect_token(char **p) {
    char buf[MAX_LINE];
    int len = 0;
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
        while (**p && **p != ' ' && **p != '\t' && **p != '|' && **p != '<' &&
               **p != '>' && **p != '&' && **p != ';' && len < MAX_LINE - 1) {
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
        while (**p && **p != ' ' && **p != '\t' && **p != '|' && **p != '<' &&
               **p != '>' && **p != '&' && **p != ';' && len < MAX_LINE - 1) {
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
    return NULL;
}

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
        } else {
            fprintf(stderr, "syntax error: unmatched '%c'\n", quote);
            return NULL;
        }
    } else {
        (*p)++;
        int first = 1;
        while (**p && **p != quote) {
            if (**p == '$' && *(*p + 1) == '(' && *(*p + 2) == '(') {
                int depth = 1;
                if (len < MAX_LINE - 1) buf[len++] = *(*p)++;
                if (len < MAX_LINE - 1) buf[len++] = *(*p)++;
                if (len < MAX_LINE - 1) buf[len++] = *(*p)++;
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
                char *res = parse_substitution(p);
                if (!res) return NULL;
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
                (*p) += 2;
                char name[MAX_LINE];
                int n = 0;
                while (**p && **p != '}' && n < MAX_LINE - 1) {
                    name[n++] = **p;
                    (*p)++;
                }
                if (**p == '}') {
                    (*p)++;
                    name[n] = '\0';
                    char varbuf[MAX_LINE + 4];
                    int needed = snprintf(varbuf, sizeof(varbuf), "${%s}", name);
                    if (needed < 0 || needed >= (int)sizeof(varbuf)) {
                        fprintf(stderr, "variable expansion too long\n");
                        return NULL;
                    }
                    char *res = expand_var(varbuf);
                    for (int ci = 0; res[ci] && len < MAX_LINE - 1; ci++)
                        buf[len++] = res[ci];
                    free(res);
                    first = 0;
                    continue;
                }
                *p = (char *)start;
            }
            if (len < MAX_LINE - 1) buf[len++] = **p;
            (*p)++;
            first = 0;
        }
        if (**p == quote) {
            (*p)++;
        } else {
            fprintf(stderr, "syntax error: unmatched '\"'\n");
            return NULL;
        }
    }
    buf[len] = '\0';
    if (do_expand_out) *do_expand_out = do_expand;
    return do_expand ? expand_var(buf) : strdup(buf);
}

char *read_token(char **p, int *quoted) {
    char buf[MAX_LINE];
    int len = 0;
    int do_expand = 1;
    *quoted = 0;
    char *redir = parse_redirect_token(p);
    if (redir)
        return redir;
    if (**p == '\'' || **p == '"') {
        return parse_quoted_word(p, quoted, &do_expand);
    }
    int first = 1;
    while (**p && (**p != ' ' && **p != '\t' && **p != '|' &&
            **p != '<' && **p != '>' && **p != '&' && **p != ';')) {
        if (**p == '$' && *(*p + 1) == '(' && *(*p + 2) == '(') {
            int depth = 1;
            if (len < MAX_LINE - 1) buf[len++] = *(*p)++;
            if (len < MAX_LINE - 1) buf[len++] = *(*p)++;
            if (len < MAX_LINE - 1) buf[len++] = *(*p)++;
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
            char *res = parse_substitution(p);
            if (!res) return NULL;
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
            (*p) += 2;
            char name[MAX_LINE];
            int n = 0;
            while (**p && **p != '}' && n < MAX_LINE - 1) {
                name[n++] = **p;
                (*p)++;
            }
            if (**p == '}') {
                (*p)++;
                name[n] = '\0';
                char varbuf[MAX_LINE + 4];
                int needed = snprintf(varbuf, sizeof(varbuf), "${%s}", name);
                if (needed < 0 || needed >= (int)sizeof(varbuf)) {
                    fprintf(stderr, "variable expansion too long\n");
                    return NULL;
                }
                char *res = expand_var(varbuf);
                for (int ci = 0; res[ci] && len < MAX_LINE - 1; ci++)
                    buf[len++] = res[ci];
                free(res);
                first = 0;
                continue;
            }
            *p = (char *)start;
        }
        if (len < MAX_LINE - 1) buf[len++] = **p;
        (*p)++;
        first = 0;
    }
    buf[len] = '\0';
    char *res = do_expand ? expand_var(buf) : strdup(buf);
    if (getenv("VUSH_DEBUG"))
        fprintf(stderr, "read_token: '%s'\n", res);
    return res;
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
            char inner[MAX_LINE];
            size_t ilen = (size_t)(end - (token + 2));
            if (ilen >= sizeof(inner)) ilen = sizeof(inner) - 1;
            memcpy(inner, token + 2, ilen);
            inner[ilen] = '\0';

            if (inner[0] == '#') {
                const char *name = inner + 1;
                const char *val = get_shell_var(name);
                if (!val) val = getenv(name);
                if (!val) {
                    if (opt_nounset) {
                        fprintf(stderr, "%s: unbound variable\n", name);
                        last_status = 1;
                    }
                    val = "";
                }
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", strlen(val));
                return strdup(buf);
            }

            char name[MAX_LINE];
            int n = 0;
            const char *p = inner;
            while (*p && *p != ':' && *p != '#' && *p != '%' && n < MAX_LINE - 1)
                name[n++] = *p++;
            name[n] = '\0';

            const char *val = NULL;
            char *lb = strchr(name, '[');
            if (lb && name[strlen(name)-1] == ']') {
                *lb = '\0';
                char *idxstr = lb + 1;
                idxstr[strlen(idxstr)-1] = '\0';
                if (strcmp(idxstr, "@") == 0) {
                    int alen = 0; char **arr = get_shell_array(name, &alen);
                    if (arr) {
                        size_t tlen = 0; for(int ai=0; ai<alen; ai++) tlen += strlen(arr[ai]) + 1;
                        char *joined = malloc(tlen+1); if(joined){ joined[0]='\0'; for(int ai=0; ai<alen; ai++){ strcat(joined, arr[ai]); if(ai<alen-1) strcat(joined, " "); }}
                        return joined ? joined : strdup("");
                    }
                    val = getenv(name);
                    if (!val) val = "";
                    char *ret = strdup(val);
                    return ret;
                } else {
                    int idx = atoi(idxstr);
                    int alen = 0; char **arr = get_shell_array(name, &alen);
                    if (arr && idx >=0 && idx < alen) return strdup(arr[idx]);
                    val = getenv(name);
                    if (!val) val = "";
                    *lb='['; // restore
                    return strdup(val);
                }
            } else {
                val = get_shell_var(name);
                if (!val) val = getenv(name);
            }

            if (*p == ':' && (p[1] == '-' || p[1] == '=' || p[1] == '+')) {
                char op = p[1];
                const char *word = p + 2;
                char *wexp = strdup(word ? word : "");
                if (!wexp) wexp = strdup("");

                int use_word = (!val || val[0] == '\0');
                if (op == '+')
                    use_word = (val && val[0] != '\0');
                if (op == '=') {
                    if (!val || val[0] == '\0') {
                        set_shell_var(name, wexp);
                        if (getenv(name))
                            setenv(name, wexp, 1);
                        val = wexp;
                    }
                }

                if (use_word)
                    return wexp;

                free(wexp);
                if (!val) {
                    if (opt_nounset) {
                        fprintf(stderr, "%s: unbound variable\n", name);
                        last_status = 1;
                    }
                    val = "";
                }
                return strdup(val);
            } else if (*p == '#' || *p == '%') {
                char op = *p;
                const char *pattern = p + 1;
                if (!val) val = "";
                size_t vlen = strlen(val);
                if (op == '#') {
                    for (size_t i = 0; i <= vlen; i++) {
                        char *pref = strndup(val, i);
                        if (!pref) break;
                        int m = fnmatch(pattern, pref, 0);
                        free(pref);
                        if (m == 0)
                            return strdup(val + i);
                    }
                    return strdup(val);
                } else {
                    for (size_t i = 0; i <= vlen; i++) {
                        char *suf = strdup(val + vlen - i);
                        if (!suf) break;
                        int m = fnmatch(pattern, suf, 0);
                        free(suf);
                        if (m == 0) {
                            char *res = strndup(val, vlen - i);
                            return res ? res : strdup("");
                        }
                    }
                    return strdup(val);
                }
            } else {
                if (!val) {
                    if (opt_nounset) {
                        fprintf(stderr, "%s: unbound variable\n", name);
                        last_status = 1;
                    }
                    val = "";
                }
                return strdup(val);
            }
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
    const char *val = get_shell_var(token + 1);
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

/* Expand simple brace patterns like {foo,bar} or {1..3}. Returns an array
 * of allocated strings terminated by NULL. The caller must free each string
 * and the array itself. If no expansion occurs the array contains a single
 * copy of WORD. Nested braces are not supported. */
char **expand_braces(const char *word, int *count_out) {
    if (count_out) *count_out = 0;
    const char *lb = strchr(word, '{');
    const char *rb = lb ? strchr(lb, '}') : NULL;
    if (!lb || !rb || rb < lb) {
        char **res = malloc(2 * sizeof(char *));
        if (!res) return NULL;
        res[0] = strdup(word);
        res[1] = NULL;
        if (count_out) *count_out = 1;
        return res;
    }

    char prefix[MAX_LINE];
    size_t prelen = (size_t)(lb - word);
    if (prelen >= sizeof(prefix)) prelen = sizeof(prefix) - 1;
    memcpy(prefix, word, prelen);
    prefix[prelen] = '\0';

    char inner[MAX_LINE];
    size_t inlen = (size_t)(rb - lb - 1);
    if (inlen >= sizeof(inner)) inlen = sizeof(inner) - 1;
    memcpy(inner, lb + 1, inlen);
    inner[inlen] = '\0';

    char suffix[MAX_LINE];
    strncpy(suffix, rb + 1, sizeof(suffix));
    suffix[sizeof(suffix) - 1] = '\0';

    char **res = malloc(sizeof(char *) * MAX_TOKENS);
    if (!res) return NULL;
    int count = 0;

    char *dots = strstr(inner, "..");
    if (dots) {
        char left[32], right[32];
        size_t llen = (size_t)(dots - inner);
        if (llen >= sizeof(left)) llen = sizeof(left) - 1;
        memcpy(left, inner, llen);
        left[llen] = '\0';
        strncpy(right, dots + 2, sizeof(right));
        right[sizeof(right) - 1] = '\0';
        char *ep1, *ep2;
        long start = strtol(left, &ep1, 10);
        long end = strtol(right, &ep2, 10);
        if (*ep1 == '\0' && *ep2 == '\0') {
            int step = start <= end ? 1 : -1;
            for (long n = start; (step > 0 ? n <= end : n >= end) && count < MAX_TOKENS - 1; n += step) {
                char num[32];
                snprintf(num, sizeof(num), "%ld", n);
                char tmp[MAX_LINE];
                snprintf(tmp, sizeof(tmp), "%s%s%s", prefix, num, suffix);
                res[count++] = strdup(tmp);
            }
            res[count] = NULL;
            if (count_out) *count_out = count;
            return res;
        }
    }

    char *dup = strdup(inner);
    if (!dup) {
        free(res);
        return NULL;
    }
    char *sp = NULL;
    char *tok = strtok_r(dup, ",", &sp);
    while (tok && count < MAX_TOKENS - 1) {
        char tmp[MAX_LINE];
        snprintf(tmp, sizeof(tmp), "%s%s%s", prefix, tok, suffix);
        res[count++] = strdup(tmp);
        tok = strtok_r(NULL, ",", &sp);
    }
    free(dup);
    if (count == 0) {
        res[count++] = strdup(word);
    }
    res[count] = NULL;
    if (count_out) *count_out = count;
    return res;
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
