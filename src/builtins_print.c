/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Builtins for echo and printf.
 */

/*
 * Printing related builtin commands: echo and printf
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "builtin_options.h"
#include <stdio.h>
#include <stdlib.h>
#include "shell_state.h"
#include "vars.h"
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include "util.h"


/* Print arguments separated by spaces. Supports -n to suppress the
 * trailing newline and -e to interpret common backslash escapes. */
int builtin_echo(char **args)
{
    int newline = 1;
    int interpret = 0;
    int i = 1;
    for (; args[i] && args[i][0] == '-' && args[i][1]; i++) {
        if (strcmp(args[i], "-n") == 0) {
            newline = 0;
            continue;
        }
        if (strcmp(args[i], "-e") == 0) {
            interpret = 1;
            continue;
        }
        break;
    }
    int first_arg = i;

    for (; args[i]; i++) {
        if (i > first_arg)
            putchar(' ');
        const char *s = args[i];
        if (interpret) {
            for (const char *p = s; *p; p++) {
                if (*p == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                    case 'n': putchar('\n'); break;
                    case 't': putchar('\t'); break;
                    case 'r': putchar('\r'); break;
                    case 'b': putchar('\b'); break;
                    case 'a': putchar('\a'); break;
                    case 'f': putchar('\f'); break;
                    case 'v': putchar('\v'); break;
                    case '\\': putchar('\\'); break;
                    default: putchar('\\'); putchar(*p); break;
                    }
                } else {
                    putchar(*p);
                }
            }
        } else {
            fputs(s, stdout);
        }
    }
    if (newline)
        putchar('\n');
    fflush(stdout);
    last_status = 0;
    return 1;
}

/* Helper parsing the next printf format specification. */
static const char *next_format_spec(const char *p, char spec[32], char *conv,
                                    int *err)
{
    const int max = 32;
    int si = 0;
    int overflow = 0;
    if (*p != '%') {
        spec[0] = '\0';
        *conv = '\0';
        if (err) *err = 0;
        return p;
    }

    if (si < max - 1)
        spec[si++] = *p;
    else
        overflow = 1;
    p++;

    if (*p == '%') {
        if (si < max - 1)
            spec[si++] = *p;
        else
            overflow = 1;
        p++;
        if (si < max)
            spec[si] = '\0';
        else
            spec[max - 1] = '\0';
        *conv = '%';
        if (err) *err = overflow;
        return p;
    }

    while (*p && strchr("-+ #0", *p)) {
        if (si < max - 1)
            spec[si++] = *p;
        else
            overflow = 1;
        p++;
    }
    while (*p && isdigit((unsigned char)*p)) {
        if (si < max - 1)
            spec[si++] = *p;
        else
            overflow = 1;
        p++;
    }
    if (*p == '.') {
        if (si < max - 1)
            spec[si++] = *p;
        else
            overflow = 1;
        p++;
        while (*p && isdigit((unsigned char)*p)) {
            if (si < max - 1)
                spec[si++] = *p;
            else
                overflow = 1;
            p++;
        }
    }
    if (strchr("hlLjzt", *p)) {
        if (si < max - 1)
            spec[si++] = *p;
        else
            overflow = 1;
        p++;
        if ((*p == 'h' && spec[si-1] == 'h') ||
            (*p == 'l' && spec[si-1] == 'l')) {
            if (si < max - 1)
                spec[si++] = *p;
            else
                overflow = 1;
            p++;
        }
    }

    if (*p) {
        *conv = *p;
        if (si < max - 1)
            spec[si++] = *p;
        else
            overflow = 1;
        p++;
    } else {
        *conv = '\0';
    }

    if (si < max)
        spec[si] = '\0';
    else
        spec[max - 1] = '\0';
    if (err) *err = overflow;
    return p;
}

/* Translate backslash escapes like \n and \0NNN in SRC.  Returns newly
 * allocated memory which the caller must free. */
static char *unescape_string(const char *src)
{
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

/* Append STR either to OUT when non-NULL or to OUTBUF/OUTSIZE.  Uses
 * open_memstream when available to keep the code common between the
 * standard printf loop and the -v case.  Returns 0 on allocation or
 * write failure. */
static int fmt_output_append(FILE *out, char **outbuf, size_t *outsize,
                             const char *str) {
#ifdef HAVE_OPEN_MEMSTREAM
    if (out)
        return fputs(str, out) >= 0;
#else
    (void)out;
#endif
    return append_str(outbuf, outsize, str);
}

/* Formatted printing similar to printf(1); stores result in last_status. */
int builtin_printf(char **args)
{
    const char *varname = NULL;
    int i = parse_builtin_options(args, "v:", &varname);
    if (i < 0 || !args[i]) {
        fprintf(stderr, "usage: printf [-v VAR] format [args...]\n");
        last_status = 1;
        return 1;
    }
    const char *srcfmt = args[i];
    char *fmt = unescape_string(srcfmt);
    if (!fmt) {
        perror("printf");
        last_status = 1;
        return 1;
    }
    FILE *out = stdout;
    char *outbuf = NULL;
    size_t outsize = 0;
#ifdef HAVE_OPEN_MEMSTREAM
    if (varname) {
        out = open_memstream(&outbuf, &outsize);
        if (!out) {
            perror("printf");
            free(fmt);
            last_status = 1;
            return 1;
        }
    }
#else
    (void)out;
#endif
    int ai = i + 1;
#ifndef HAVE_OPEN_MEMSTREAM
    if (varname) {
        out = NULL;
    }
#endif

    for (const char *p = fmt; *p; ) {
        if (*p != '%') {
            char ch[2] = { *p++, '\0' };
            if (!fmt_output_append(out, &outbuf, &outsize, ch)) {
                perror("printf");
                if (varname && out)
                    fclose(out);
                free(outbuf);
                free(fmt);
                last_status = 1;
                return 1;
            }
            continue;
        }
        char spec[32];
        char conv;
        int err = 0;
        p = next_format_spec(p, spec, &conv, &err);
        if (err) {
            fprintf(stderr, "printf: format specifier too long\n");
            if (varname && out)
                fclose(out);
            free(outbuf);
            free(fmt);
            last_status = 1;
            return 1;
        }
        if (!conv)
            break;
        if (conv == '%') {
            if (!fmt_output_append(out, &outbuf, &outsize, "%")) {
                perror("printf");
                if (varname && out)
                    fclose(out);
                free(outbuf);
                free(fmt);
                last_status = 1;
                return 1;
            }
            continue;
        }

        char *arg = args[ai] ? args[ai] : "";
        char *tmp = NULL;
        switch (conv) {
        case 'd': case 'i':
            if (xasprintf(&tmp, spec, (long long)strtoll(arg, NULL, 0)) < 0)
                tmp = NULL;
            if (args[ai]) ai++;
            break;
        case 'u': case 'o': case 'x': case 'X':
            if (xasprintf(&tmp, spec, (unsigned long long)strtoull(arg, NULL, 0)) < 0)
                tmp = NULL;
            if (args[ai]) ai++;
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            if (xasprintf(&tmp, spec, strtod(arg, NULL)) < 0)
                tmp = NULL;
            if (args[ai]) ai++;
            break;
        case 'c':
            if (xasprintf(&tmp, spec, arg[0]) < 0)
                tmp = NULL;
            if (args[ai]) ai++;
            break;
        case 's':
            if (xasprintf(&tmp, spec, arg) < 0)
                tmp = NULL;
            if (args[ai]) ai++;
            break;
        case 'b': {
            size_t len = strlen(arg);
            char *buf = malloc(len + 1);
            if (!buf) {
                perror("printf");
                if (varname && out)
                    fclose(out);
                free(outbuf);
                free(fmt);
                last_status = 1;
                return 1;
            }
            char *bp = buf;
            for (const char *p2 = arg; *p2; p2++) {
                if (*p2 == '\\' && p2[1]) {
                    p2++;
                    switch (*p2) {
                    case 'n': *bp++ = '\n'; break;
                    case 't': *bp++ = '\t'; break;
                    case 'r': *bp++ = '\r'; break;
                    case 'b': *bp++ = '\b'; break;
                    case 'a': *bp++ = '\a'; break;
                    case 'f': *bp++ = '\f'; break;
                    case 'v': *bp++ = '\v'; break;
                    case '\\': *bp++ = '\\'; break;
                    default: *bp++ = '\\'; *bp++ = *p2; break;
                    }
                } else {
                    *bp++ = *p2;
                }
            }
            *bp = '\0';
            spec[strlen(spec) - 1] = 's';
            if (xasprintf(&tmp, spec, buf) < 0)
                tmp = NULL;
            free(buf);
            if (args[ai]) ai++;
            break;
        }
        case 'p':
            if (xasprintf(&tmp, spec, (void *)(uintptr_t)strtoull(arg, NULL, 0)) < 0)
                tmp = NULL;
            if (args[ai]) ai++;
            break;
        default:
            tmp = strdup(spec);
            break;
        }
        if (!tmp || !fmt_output_append(out, &outbuf, &outsize, tmp)) {
            perror("printf");
            free(tmp);
            if (varname && out)
                fclose(out);
            free(outbuf);
            free(fmt);
            last_status = 1;
            return 1;
        }
        free(tmp);
    }

#ifdef HAVE_OPEN_MEMSTREAM
    if (varname) {
        fflush(out);
        fclose(out);
        set_shell_var(varname, outbuf ? outbuf : "");
        free(outbuf);
        free(fmt);
        last_status = 0;
        return 1;
    }
#else
    if (varname) {
        set_shell_var(varname, outbuf ? outbuf : "");
        free(outbuf);
        free(fmt);
        last_status = 0;
        return 1;
    }
#endif

    fflush(stdout);
    free(fmt);
    last_status = 0;
    return 1;
}
