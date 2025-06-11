#define _GNU_SOURCE
#include "lexer.h"
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

extern int last_status;

/* Execute CMD via popen and return its stdout output as a newly
 * allocated string with any trailing newline removed. */
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

/* Parse a command substitution starting at *p. Supports both $(...) and
 * backtick forms. On success *p is advanced past the closing delimiter and
 * the command's output is returned. NULL is returned on syntax errors. */
char *parse_substitution(char **p) {
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
    if (!**p) {
        parse_need_more = 1;
        return NULL;
    }
    if (is_dollar) {
        if (depth > 0) {
            parse_need_more = 1;
            return NULL;
        }
    } else {
        if (**p == '`')
            (*p)++;
        else {
            parse_need_more = 1;
            return NULL;
        }
    }
    cmd[clen] = '\0';
    return command_output(cmd);
}

/* -- Expansion helper functions --------------------------------------- */

/* Expand ~ or ~user to the appropriate home directory path. */
static char *expand_tilde(const char *token) {
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

/* Evaluate an arithmetic expansion token $((expr)) and return its value. */
static char *expand_arith(const char *token) {
    size_t tlen = strlen(token);
    if (!(tlen > 4 && strncmp(token, "$((", 3) == 0 &&
          token[tlen-2] == ')' && token[tlen-1] == ')'))
        return NULL;
    char *expr = strndup(token + 3, tlen - 5);
    if (!expr) return strdup("");
    long val = eval_arith(expr);
    free(expr);
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", val);
    return strdup(buf);
}

/* Expand NAME[IDX] style references. IDX may be '@' to join all elements.
 * Falls back to environment variables when the shell array is unset. */
static char *expand_array_element(const char *name, const char *idxstr) {
    if (strcmp(idxstr, "@") == 0) {
        int alen = 0; char **arr = get_shell_array(name, &alen);
        if (arr) {
            size_t tlen = 0;
            for (int ai = 0; ai < alen; ai++)
                tlen += strlen(arr[ai]) + 1;
            char *joined = malloc(tlen + 1);
            if (joined) {
                joined[0] = '\0';
                for (int ai = 0; ai < alen; ai++) {
                    strcat(joined, arr[ai]);
                    if (ai < alen - 1)
                        strcat(joined, " ");
                }
            }
            return joined ? joined : strdup("");
        }
        const char *val = getenv(name);
        if (!val) val = "";
        return strdup(val);
    } else {
        int idx = atoi(idxstr);
        int alen = 0; char **arr = get_shell_array(name, &alen);
        if (arr && idx >= 0 && idx < alen)
            return strdup(arr[idx]);
        const char *val = getenv(name);
        if (!val) val = "";
        return strdup(val);
    }
}

/* Apply parameter expansion modifiers contained in P to VAL. Handles
 * operations such as default values, assignment, removal of prefixes or
 * suffixes, and others used in ${NAMEMOD}. NAME is used for error messages
 * and assignments. */
static char *apply_modifier(const char *name, const char *val, const char *p) {
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
    } else if ((*p == ':' && p[1] == '?') || *p == '?') {
        const char *word = (*p == ':') ? p + 2 : p + 1;
        char *wexp = strdup(word && *word ? word : "");
        if (!wexp) wexp = strdup("");
        int err = (!val || val[0] == '\0');
        if (err) {
            if (*wexp)
                fprintf(stderr, "%s: %s\n", name, wexp);
            else
                fprintf(stderr, "%s: parameter null or not set\n", name);
            last_status = 1;
            free(wexp);
            return strdup("");
        }
        free(wexp);
        return strdup(val ? val : "");
    } else if (*p == ':' && isdigit((unsigned char)p[1])) {
        if (!val) {
            if (opt_nounset) {
                fprintf(stderr, "%s: unbound variable\n", name);
                last_status = 1;
            }
            val = "";
        }
        char *end;
        long off = strtol(p + 1, &end, 10);
        long len = -1;
        if (*end == ':') {
            len = strtol(end + 1, &end, 10);
        }
        size_t vlen = strlen(val);
        if (off < 0) {
            if ((size_t)(-off) > vlen) off = 0;
            else off = vlen + off;
        }
        if ((size_t)off > vlen) off = vlen;
        size_t avail = vlen - off;
        size_t count = (len < 0 || (size_t)len > avail) ? avail : (size_t)len;
        char *res = strndup(val + off, count);
        return res ? res : strdup("");
    } else if (*p == '#' || *p == '%') {
        char op = *p;
        int longest = 0;
        if (p[1] == op) {
            longest = 1;
            p++;
        }
        const char *pattern = p + 1;
        if (!val) val = "";
        size_t vlen = strlen(val);
        if (op == '#') {
            if (!longest) {
                for (size_t i = 0; i <= vlen; i++) {
                    char *pref = strndup(val, i);
                    if (!pref) break;
                    int m = fnmatch(pattern, pref, 0);
                    free(pref);
                    if (m == 0)
                        return strdup(val + i);
                }
            } else {
                for (size_t i = vlen;; i--) {
                    char *pref = strndup(val, i);
                    if (!pref) break;
                    int m = fnmatch(pattern, pref, 0);
                    free(pref);
                    if (m == 0)
                        return strdup(val + i);
                    if (i == 0)
                        break;
                }
            }
            return strdup(val);
        } else {
            if (!longest) {
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
            } else {
                for (size_t i = vlen;; i--) {
                    char *suf = strdup(val + vlen - i);
                    if (!suf) break;
                    int m = fnmatch(pattern, suf, 0);
                    free(suf);
                    if (m == 0) {
                        char *res = strndup(val, vlen - i);
                        return res ? res : strdup("");
                    }
                    if (i == 0)
                        break;
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

/* Expand ${#NAME} to the length of NAME's value. */
static char *expand_length(const char *name) {
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

/* Expand a ${...} expression contained in INNER. Handles array indexing,
 * length expansion and parameter modifiers. */
static char *expand_braced(const char *inner) {
    if (inner[0] == '#')
        return expand_length(inner + 1);

    char name[MAX_LINE];
    int n = 0;
    const char *p = inner;
    while (*p && *p != ':' && *p != '#' && *p != '%' && n < MAX_LINE - 1)
        name[n++] = *p++;
    name[n] = '\0';

    const char *val = NULL;
    char *lb = strchr(name, '[');
    if (lb && name[strlen(name) - 1] == ']') {
        *lb = '\0';
        char *idxstr = lb + 1;
        idxstr[strlen(idxstr) - 1] = '\0';
        return expand_array_element(name, idxstr);
    } else {
        val = get_shell_var(name);
        if (!val) val = getenv(name);
    }

    if (*p)
        return apply_modifier(name, val, p);

    if (!val) {
        if (opt_nounset) {
            fprintf(stderr, "%s: unbound variable\n", name);
            last_status = 1;
        }
        val = "";
    }
    return strdup(val);
}

/* Expand special parameters such as $?, $#, $@, $*, and positional arguments. */
static char *expand_special(const char *token) {
    if (strcmp(token, "$$") == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)getpid());
        return strdup(buf);
    }
    if (strcmp(token, "$!") == 0) {
        char buf[16];
        if (last_bg_pid == 0)
            buf[0] = '\0';
        else
            snprintf(buf, sizeof(buf), "%d", (int)last_bg_pid);
        return strdup(buf);
    }
    if (strcmp(token, "$?") == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", last_status);
        return strdup(buf);
    }
    if (strcmp(token, "$#") == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", script_argc);
        return strdup(buf);
    }
    if (strcmp(token, "$-") == 0) {
        char flags[16];
        int pos = 0;
        if (opt_allexport)
            flags[pos++] = 'a';
        if (opt_errexit)
            flags[pos++] = 'e';
        if (opt_noglob)
            flags[pos++] = 'f';
        if (opt_noexec)
            flags[pos++] = 'n';
        if (opt_nounset)
            flags[pos++] = 'u';
        if (opt_verbose)
            flags[pos++] = 'v';
        if (opt_xtrace)
            flags[pos++] = 'x';
        flags[pos] = '\0';
        return strdup(flags);
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
    if (strcmp(token, "$*") == 0) {
        if (!script_argv || script_argc == 0)
            return strdup("");
        const char *ifs = get_shell_var("IFS");
        if (!ifs) ifs = getenv("IFS");
        char sep = (ifs && *ifs) ? ifs[0] : ' ';
        size_t len = 0;
        for (int i = 1; i <= script_argc; i++)
            len += strlen(script_argv[i]) + 1;
        char *res = malloc(len);
        if (!res) return NULL;
        res[0] = '\0';
        for (int i = 1; i <= script_argc; i++) {
            strcat(res, script_argv[i]);
            if (i < script_argc) {
                size_t l = strlen(res);
                res[l] = sep;
                res[l + 1] = '\0';
            }
        }
        return res;
    }
    if (token[1] >= '0' && token[1] <= '9') {
        char *end;
        long idx = strtol(token + 1, &end, 10);
        if (*end == '\0') {
            const char *val = NULL;
            if (script_argv) {
                if (idx == 0)
                    val = script_argv[0];
                else if (idx <= script_argc)
                    val = script_argv[idx];
            }
            if (!val) {
                if (opt_nounset) {
                    fprintf(stderr, "%ld: unbound variable\n", idx);
                    last_status = 1;
                }
                val = "";
            }
            return strdup(val);
        }
    }
    return NULL;
}

/* Expand a normal variable name to its value or an empty string. */
static char *expand_plain_var(const char *name) {
    const char *val = get_shell_var(name);
    if (!val) val = getenv(name);
    if (!val) {
        if (opt_nounset) {
            fprintf(stderr, "%s: unbound variable\n", name);
            last_status = 1;
        }
        val = "";
    }
    return strdup(val);
}

/* Expand a TOKEN that may contain variable, arithmetic, tilde or
 * parameter expansion syntax. Returns a newly allocated string with the
 * result of the expansion. */
char *expand_var(const char *token) {
    char *s = expand_special(token);
    if (s)
        return s;
    if (token[0] == '~')
        return expand_tilde(token);
    if (token[0] != '$')
        return strdup(token);

    s = expand_arith(token);
    if (s)
        return s;

    if (token[1] == '{') {
        const char *end = strchr(token + 2, '}');
        if (end && end[1] == '\0') {
            char inner[MAX_LINE];
            size_t ilen = (size_t)(end - (token + 2));
            if (ilen >= sizeof(inner)) ilen = sizeof(inner) - 1;
            memcpy(inner, token + 2, ilen);
            inner[ilen] = '\0';
            return expand_braced(inner);
        }
    }

    return expand_plain_var(token + 1);
}

/* Perform history expansion on LINE when it begins with '!'. Returns a new
 * string with the expansion applied or NULL on error. */
char *expand_history(const char *line) {
    const char *p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '!')
        return strdup(line);
    const char *bang = p;
    const char *rest;
    char *expansion = NULL;
    char pref[MAX_LINE];
    if (p[1] == '!' && (p[2] == '\0' || isspace((unsigned char)p[2]))) {
        const char *tmp = history_last();
        if (tmp)
            expansion = strdup(tmp);
        rest = p + 2;
        if (!expansion) {
            fprintf(stderr, "history: event not found\n");
            last_status = 1;
            return NULL;
        }
    } else if (isdigit((unsigned char)p[1]) ||
               (p[1] == '-' && isdigit((unsigned char)p[2]))) {
        int neg = (p[1] == '-');
        p += neg ? 2 : 1;
        int n = 0;
        while (isdigit((unsigned char)*p) && n < MAX_LINE - 1)
            pref[n++] = *p++;
        pref[n] = '\0';
        rest = p;
        int id = atoi(pref);
        const char *tmp = neg ? history_get_relative(id + 1) : history_get_by_id(id);
        if (tmp)
            expansion = strdup(tmp);
        if (!expansion) {
            fprintf(stderr, "history: event not found: %s%s\n", neg ? "-" : "", pref);
            last_status = 1;
            return NULL;
        }
    } else if (p[1] == '$' && (p[2] == '\0' || isspace((unsigned char)p[2]))) {
        expansion = history_last_word();
        rest = p + 2;
        if (!expansion) {
            fprintf(stderr, "history: event not found: $\n");
            last_status = 1;
            return NULL;
        }
    } else if (p[1] == '*' && (p[2] == '\0' || isspace((unsigned char)p[2]))) {
        expansion = history_all_words();
        rest = p + 2;
        if (!expansion) {
            fprintf(stderr, "history: event not found: *\n");
            last_status = 1;
            return NULL;
        }
    } else {
        int n = 0;
        p++;
        while (*p && !isspace((unsigned char)*p) && n < MAX_LINE - 1)
            pref[n++] = *p++;
        pref[n] = '\0';
        const char *tmp = history_find_prefix(pref);
        if (tmp)
            expansion = strdup(tmp);
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
    free(expansion);
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
                size_t len = strlen(prefix) + strlen(num) + strlen(suffix) + 1;
                char *tmp = malloc(len);
                if (!tmp) {
                    for (int i = 0; i < count; i++)
                        free(res[i]);
                    free(res);
                    return NULL;
                }
                snprintf(tmp, len, "%s%s%s", prefix, num, suffix);
                res[count++] = tmp;
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
        size_t len = strlen(prefix) + strlen(tok) + strlen(suffix) + 1;
        char *tmp = malloc(len);
        if (!tmp) {
            free(dup);
            for (int i = 0; i < count; i++)
                free(res[i]);
            free(res);
            return NULL;
        }
        snprintf(tmp, len, "%s%s%s", prefix, tok, suffix);
        res[count++] = tmp;
        tok = strtok_r(NULL, ",", &sp);
    }
    free(dup);
    if (count == 0) {
        res[count] = strdup(word);
        if (!res[count]) {
            for (int i = 0; i < count; i++)
                free(res[i]);
            free(res);
            return NULL;
        }
        count++;
    }
    res[count] = NULL;
    if (count_out) *count_out = count;
    return res;
}

/* Expand escape sequences and variables found in PROMPT using the normal
 * token expansion logic. A new string is returned. */
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

