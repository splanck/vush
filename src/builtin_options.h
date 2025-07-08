/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Helper for parsing builtin options.
 */

#ifndef BUILTIN_OPTIONS_H
#define BUILTIN_OPTIONS_H

/* Parse ARGS according to OPTSPEC storing option values in the provided
 * pointers. OPTSPEC uses characters for each option with a trailing ':'
 * indicating that the option requires an argument. The caller must supply
 * an int* for each flag option and a const char ** for options taking an
 * argument. Returns the index of the first non-option argument or -1 on
 * invalid usage.
 */
int parse_builtin_options(char **args, const char *optspec, ...);

#endif /* BUILTIN_OPTIONS_H */
