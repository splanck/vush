#ifndef SCRIPTARGS_H
#define SCRIPTARGS_H

/*
 * Store the argument vector for the currently running script or function.
 *
 * script_argv[0] holds the script name just like $0. When a shell function is
 * invoked, these globals are replaced with copies of the function's arguments
 * so that $1, $2, ... expand correctly within the body. They are restored once
 * the function returns.
 */

/* Number of positional parameters; updated before a function body executes. */
extern int script_argc;
/* Null terminated array of parameters; swapped during function calls. */
extern char **script_argv;

#endif /* SCRIPTARGS_H */
