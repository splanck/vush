#define _GNU_SOURCE
#include "var_expand.h"
#include <stdlib.h>
#include <string.h>

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
