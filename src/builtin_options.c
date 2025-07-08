/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Helper for parsing builtin options.
 */

#include "builtin_options.h"
#include <string.h>
#include <stdarg.h>

typedef struct {
    char opt;
    int has_arg;
    void *dst;
} OptEntry;

/* Parse ARGS according to OPTSPEC and store results in the provided
 * pointers. OPTSPEC uses a single character per option with an optional
 * trailing ':' to indicate that an argument is required. For each option
 * an int* or const char ** must be supplied via varargs. Options without
 * arguments set the int to 1 when seen. Options requiring an argument
 * store the argument string. The return value is the index of the first
 * non-option argument or -1 on error.
 */
int parse_builtin_options(char **args, const char *optspec, ...) {
    OptEntry opts[32];
    int count = 0;
    va_list ap;
    va_start(ap, optspec);
    for (const char *p = optspec; *p; p++) {
        if (*p == ':')
            continue;
        opts[count].opt = *p;
        opts[count].has_arg = (p[1] == ':');
        opts[count].dst = va_arg(ap, void *);
        if (opts[count].has_arg)
            p++;
        count++;
    }
    va_end(ap);

    int i = 1;
    while (args[i] && args[i][0] == '-' && args[i][1]) {
        if (strcmp(args[i], "--") == 0) {
            i++;
            break;
        }
        int pos = 1;
        while (args[i][pos]) {
            char c = args[i][pos];
            int found = 0;
            for (int j = 0; j < count; j++) {
                if (opts[j].opt == c) {
                    found = 1;
                    if (opts[j].has_arg) {
                        const char *val = NULL;
                        if (args[i][pos + 1]) {
                            val = &args[i][pos + 1];
                            pos = (int)strlen(args[i]);
                        } else if (args[i + 1]) {
                            val = args[i + 1];
                            i++;
                        } else {
                            return -1;
                        }
                        *(const char **)opts[j].dst = val;
                        pos = (int)strlen(args[i]);
                    } else {
                        *(int *)opts[j].dst = 1;
                    }
                    break;
                }
            }
            if (!found)
                return -1;
            pos++;
        }
        i++;
    }
    return i;
}
