#define _GNU_SOURCE
#include "var_expand.h"
#include "lexer.h"
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
extern int param_error;


char **split_fields(const char *text, int *count_out) {
    if (count_out)
        *count_out = 0;

    const char *ifs = get_shell_var("IFS");
    if (!ifs)
        ifs = getenv("IFS");
    if (!ifs)
        ifs = " \t\n";

    /* Empty IFS disables splitting completely. */
    if (!*ifs) {
        char **res = malloc(2 * sizeof(char *));
        if (!res)
            return NULL;
        res[0] = strdup(text);
        if (!res[0]) {
            free(res);
            return NULL;
        }
        res[1] = NULL;
        if (count_out)
            *count_out = 1;
        return res;
    }

    char ws_tab[256] = {0};
    char other_tab[256] = {0};
    for (const char *p = ifs; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '\t' || c == '\n')
            ws_tab[c] = 1;
        else
            other_tab[c] = 1;
    }

    char *dup = strdup(text);
    if (!dup)
        return NULL;
    char *p = dup;
    char *field_start = dup;
    char **out = NULL;
    int count = 0;
    int last_nonspace = 0;

    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (ws_tab[c]) {
            if (p > field_start) {
                char save = *p;
                *p = '\0';
                char **tmp = realloc(out, sizeof(char *) * (count + 1));
                if (!tmp) {
                    goto fail;
                }
                out = tmp;
                out[count] = strdup(field_start);
                if (!out[count]) {
                    goto fail;
                }
                count++;
                *p = save;
            }
            while (ws_tab[(unsigned char)*p])
                p++;
            field_start = p;
            last_nonspace = 0;
            continue;
        } else if (other_tab[c]) {
            char save = *p;
            *p = '\0';
            char **tmp = realloc(out, sizeof(char *) * (count + 1));
            if (!tmp) {
                goto fail;
            }
            out = tmp;
            out[count] = strdup(field_start);
            if (!out[count]) {
                goto fail;
            }
            count++;
            *p = save;
            p++;
            field_start = p;
            last_nonspace = 1;
            continue;
        }
        last_nonspace = 0;
        p++;
    }

    if (p > field_start || last_nonspace) {
        char **tmp = realloc(out, sizeof(char *) * (count + 1));
        if (!tmp) {
            goto fail;
        }
        out = tmp;
        out[count] = strdup(field_start);
        if (!out[count]) {
            goto fail;
        }
        count++;
    }

    free(dup);

    if (count == 0) {
        out = malloc(sizeof(char *));
        if (!out)
            return NULL;
        out[0] = NULL;
    } else {
        char **tmp = realloc(out, sizeof(char *) * (count + 1));
        if (!tmp) {
            goto fail_alloc;
        }
        out = tmp;
        out[count] = NULL;
    }
    if (count_out)
        *count_out = count;
    return out;

fail:
    free(dup);
fail_alloc:
    if (out) {
        for (int i = 0; i < count; i++)
            free(out[i]);
        free(out);
    }
    if (count_out)
        *count_out = 0;
    return NULL;
}

/* -- Expansion helper functions --------------------------------------- */

/* Translate backslash escapes like \n and \0NNN used by $'..' quoting. */
char *ansi_unescape(const char *src) {
    size_t len = strlen(src);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    char *d = out;
    for (const char *s = src; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'n': *d++ = '\n'; break;
            case 't': *d++ = '\t'; break;
            case 'r': *d++ = '\r'; break;
            case 'b': *d++ = '\b'; break;
            case 'a': *d++ = '\a'; break;
            case 'f': *d++ = '\f'; break;
            case 'v': *d++ = '\v'; break;
            case '\\': *d++ = '\\'; break;
            case '\'': *d++ = '\''; break;
            case '"': *d++ = '"'; break;
            case '0': {
                int val = 0, cnt = 0;
                while (cnt < 3 && s[1] >= '0' && s[1] <= '7') {
                    s++; cnt++; val = val * 8 + (*s - '0');
                }
                *d++ = (char)val;
                break;
            }
            default:
                *d++ = '\\';
                *d++ = *s;
                break;
            }
        } else {
            *d++ = *s;
        }
    }
    *d = '\0';
    return out;
}

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
            setpwent();
            struct passwd *pw = getpwnam(user);
            endpwent();
            if (pw) {
                home = pw->pw_dir;
            } else {
                fprintf(stderr, "cd: %s: no such user\n", user);
                last_status = 1;
                free(user);
                return NULL;
            }
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
    while (tlen > 0 && (token[tlen-1] == '\n' || token[tlen-1] == '\r'))
        tlen--;
    if (!(tlen > 4 && strncmp(token, "$((", 3) == 0 &&
          token[tlen-2] == ')' && token[tlen-1] == ')'))
        return NULL;
    char *expr = strndup(token + 3, tlen - 5);
    if (!expr) return strdup("");
    int err = 0;
    char *msg = NULL;
    long val = eval_arith(expr, &err, &msg);
    free(expr);
    if (err) {
        if (msg) {
            fprintf(stderr, "arith: %s\n", msg);
            free(msg);
        }
        param_error = 1;
        last_status = 1;
    }
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
        if (arr) {
            if (idx >= 0 && idx < alen)
                return strdup(arr[idx]);
            return strdup("");
        }
        const char *val = getenv(name);
        if (!val) val = "";
        return strdup(val);
    }
}

/* Find the first substring of TEXT that matches PAT using fnmatch().
 * On success *START and *LEN indicate the matched range and 1 is returned.
 * Returns 0 if no match is found. */
static int find_glob_substring(const char *text, const char *pat,
                               size_t *start, size_t *len) {
    size_t tlen = strlen(text);
    char buf[MAX_LINE];
    for (size_t i = 0; i < tlen; i++) {
        size_t remain = tlen - i;
        for (size_t l = remain; l > 0; l--) {
            if (l >= sizeof(buf))
                continue;
            memcpy(buf, text + i, l);
            buf[l] = '\0';
            if (fnmatch(pat, buf, 0) == 0) {
                if (start) *start = i;
                if (len) *len = l;
                return 1;
            }
        }
    }
    return 0;
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
                param_error = 1;
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
            param_error = 1;
            free(wexp);
            return strdup("");
        }
        free(wexp);
        return strdup(val ? val : "");
    } else if (*p == '/' ) {
        int global = 0;
        if (p[1] == '/') { global = 1; p++; }
        const char *pat = p + 1;
        const char *sep = strchr(pat, '/');
        if (!sep) {
            if (!val) val = "";
            return strdup(val);
        }
        char *pattern = strndup(pat, sep - pat);
        const char *repl = sep + 1;
        if (!pattern) return strdup(val ? val : "");
        if (!val) val = "";
        size_t vlen = strlen(val);
        size_t rlen = strlen(repl);
        size_t cap = vlen * (rlen ? rlen : 1) + 1;
        char *res = malloc(cap);
        if (!res) { free(pattern); return NULL; }
        size_t out = 0;
        size_t pos = 0;
        while (pos < vlen) {
            size_t s, l;
            if (find_glob_substring(val + pos, pattern, &s, &l)) {
                memcpy(res + out, val + pos, s);
                out += s;
                memcpy(res + out, repl, rlen);
                out += rlen;
                pos += s + l;
                if (!global)
                    break;
            } else {
                size_t rest = vlen - pos;
                memcpy(res + out, val + pos, rest);
                out += rest;
                pos = vlen;
            }
        }
        if (pos < vlen) {
            memcpy(res + out, val + pos, vlen - pos);
            out += vlen - pos;
        }
        res[out] = '\0';
        free(pattern);
        char *ret = strdup(res);
        free(res);
        return ret ? ret : strdup("");
    } else if (*p == ':' && isdigit((unsigned char)p[1])) {
        if (!val) {
            if (opt_nounset) {
                fprintf(stderr, "%s: unbound variable\n", name);
                last_status = 1;
                param_error = 1;
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
            param_error = 1;
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
    while (*p && *p != ':' && *p != '#' && *p != '%' && *p != '/' && *p != '?' && n < MAX_LINE - 1)
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
    if (strcmp(token, "$PPID") == 0 || strcmp(token, "${PPID}") == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)parent_pid);
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
    if (strcmp(token, "$LINENO") == 0 || strcmp(token, "${LINENO}") == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current_lineno);
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
                    param_error = 1;
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
            param_error = 1;
        }
        val = "";
    }
    return strdup(val);
}

/* Expand a TOKEN that may contain variable, arithmetic, tilde or
 * parameter expansion syntax. Returns a newly allocated string with the
 * result of the expansion. */
/* Existing variable expansion logic that handles a single expansion token. */
char *expand_simple(const char *token) {
    char *s = expand_special(token);
    if (s)
        return s;

    if (token[0] == '~')
        return expand_tilde(token);

    /* Handle arithmetic expansion before command substitution so that
     * tokens like $((1+2)) are not treated as $( command ). */
    if (token[0] == '$') {
        s = expand_arith(token);
        if (s)
            return s;
    }

    if (token[0] == '`' || (token[0] == '$' && token[1] == '(' && token[2] != '(')) {
        char *p = (char *)token;
        char *out = parse_substitution(&p);
        if (out && *p == '\0')
            return out;
        free(out);
        /* fall through to regular expansion on failure */
    }

    if (token[0] != '$')
        return strdup(token);

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

/* Expand escape sequences and variables found in PROMPT using the normal
 * token expansion logic. A new string is returned. */
char *expand_prompt(const char *prompt) {
    if (!prompt)
        return strdup("");

    /* Wrap the prompt in double quotes so the normal token reader can
     * interpret backslash escapes and quoting rules. */
    size_t len = strlen(prompt);
    char *tmp = malloc(len + 3);
    if (!tmp)
        return strdup("");
    tmp[0] = '"';
    memcpy(tmp + 1, prompt, len);
    tmp[len + 1] = '"';
    tmp[len + 2] = '\0';

    char *p = tmp;
    int quoted = 0;
    int do_expand = 1;
    char *res = read_token(&p, &quoted, &do_expand);
    if (getenv("VUSH_DEBUG"))
        fprintf(stderr, "expand_prompt token='%s' de=%d\n", res ? res : "", do_expand);
    free(tmp);
    if (!res)
        return strdup("");

    /* When expansion is requested run the normal variable expansion logic.
     * This interprets variables and command substitutions while leaving any
     * trailing whitespace intact. */
    if (do_expand) {
        char *out = expand_var(res);
        if (!out) {
            free(res);
            return strdup("");
        }
        if (getenv("VUSH_DEBUG"))
            fprintf(stderr, "expand_prompt result='%s'\n", out);
        free(res);
        res = out;
    }

    return res;
}

