/*
 * Printing related builtin commands: echo and printf
 */
#define _GNU_SOURCE
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

extern int last_status;

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
static const char *next_format_spec(const char *p, char spec[32], char *conv)
{
    int si = 0;
    if (*p != '%') {
        spec[0] = '\0';
        *conv = '\0';
        return p;
    }

    spec[si++] = *p++;

    if (*p == '%') {
        spec[si++] = *p++;
        spec[si] = '\0';
        *conv = '%';
        return p;
    }

    while (*p && strchr("-+ #0", *p))
        spec[si++] = *p++;
    while (*p && isdigit((unsigned char)*p))
        spec[si++] = *p++;
    if (*p == '.') {
        spec[si++] = *p++;
        while (*p && isdigit((unsigned char)*p))
            spec[si++] = *p++;
    }
    if (strchr("hlLjzt", *p)) {
        spec[si++] = *p++;
        if ((*p == 'h' && spec[si-1] == 'h') ||
            (*p == 'l' && spec[si-1] == 'l'))
            spec[si++] = *p++;
    }

    if (*p) {
        *conv = *p;
        spec[si++] = *p++;
    } else {
        *conv = '\0';
    }

    spec[si] = '\0';
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
    const char *srcfmt = args[1] ? args[1] : "";
    char *fmt = unescape_string(srcfmt);
    if (!fmt) {
        perror("printf");
        last_status = 1;
        return 1;
    }
    int ai = 2;
    for (const char *p = fmt; *p; ) {
        if (*p != '%') {
            putchar(*p++);
            continue;
        }
        char spec[32];
        char conv;
        p = next_format_spec(p, spec, &conv);
        if (!conv)
            break;
        if (conv == '%') {
            putchar('%');
            continue;
        }

        char *arg = args[ai] ? args[ai] : "";
        switch (conv) {
        case 'd': case 'i':
            printf(spec, (long long)strtoll(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        case 'u': case 'o': case 'x': case 'X':
            printf(spec, (unsigned long long)strtoull(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            printf(spec, strtod(arg, NULL));
            if (args[ai]) ai++;
            break;
        case 'c':
            printf(spec, arg[0]);
            if (args[ai]) ai++;
            break;
        case 's':
            printf(spec, arg);
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
            printf(spec, buf);
            free(buf);
            if (args[ai]) ai++;
            break;
        }
        case 'p':
            printf(spec, (void *)(uintptr_t)strtoull(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        default:
            fputs(spec, stdout);
            break;
        }
    }
    fflush(stdout);
    free(fmt);
    last_status = 0;
    return 1;
}
