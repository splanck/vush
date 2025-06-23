/*
 * Printing related builtin commands: echo and printf
 */
#define _GNU_SOURCE
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include "shell_state.h"
#include "vars.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>


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

/* Formatted printing similar to printf(1); stores result in last_status. */
int builtin_printf(char **args)
{
    const char *varname = NULL;
    int i = 1;
    if (args[i] && strcmp(args[i], "-v") == 0) {
        if (!args[i+1]) {
            fprintf(stderr, "usage: printf [-v VAR] format [args...]\n");
            last_status = 1;
            return 1;
        }
        varname = args[i+1];
        i += 2;
    }
    if (!args[i]) {
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
    if (varname) {
        out = open_memstream(&outbuf, &outsize);
        if (!out) {
            perror("printf");
            free(fmt);
            last_status = 1;
            return 1;
        }
    }
    int ai = i + 1;
    for (const char *p = fmt; *p; ) {
        if (*p != '%') {
            fputc(*p++, out);
            continue;
        }
        char spec[32];
        char conv;
        int err = 0;
        p = next_format_spec(p, spec, &conv, &err);
        if (err) {
            fprintf(stderr, "printf: format specifier too long\n");
            if (varname) {
                fclose(out);
                free(outbuf);
            }
            free(fmt);
            last_status = 1;
            return 1;
        }
        if (!conv)
            break;
        if (conv == '%') {
            fputc('%', out);
            continue;
        }

        char *arg = args[ai] ? args[ai] : "";
        switch (conv) {
        case 'd': case 'i':
            fprintf(out, spec, (long long)strtoll(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        case 'u': case 'o': case 'x': case 'X':
            fprintf(out, spec, (unsigned long long)strtoull(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            fprintf(out, spec, strtod(arg, NULL));
            if (args[ai]) ai++;
            break;
        case 'c':
            fprintf(out, spec, arg[0]);
            if (args[ai]) ai++;
            break;
        case 's':
            fprintf(out, spec, arg);
            if (args[ai]) ai++;
            break;
        case 'b': {
            size_t len = strlen(arg);
            char *buf = malloc(len + 1);
            if (!buf) {
                perror("printf");
                last_status = 1;
                free(fmt);
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
            fprintf(out, spec, buf);
            free(buf);
            if (args[ai]) ai++;
            break;
        }
        case 'p':
            fprintf(out, spec, (void *)(uintptr_t)strtoull(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        default:
            fputs(spec, out);
            break;
        }
    }
    fflush(out);
    if (varname) {
        fclose(out);
        set_shell_var(varname, outbuf ? outbuf : "");
        free(outbuf);
    } else {
        fflush(stdout);
    }
    free(fmt);
    last_status = 0;
    return 1;
}
