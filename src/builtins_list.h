/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Builtin command list macro.
 */

/* List of builtin commands: enum id, string name, and function */
/* id must be a valid identifier without prefix */
/* This file is meant to be included multiple times with different
 * definitions of the DEF_BUILTIN macro. */

DEF_BUILTIN(CD, "cd", builtin_cd)
DEF_BUILTIN(PUSHD, "pushd", builtin_pushd)
DEF_BUILTIN(POPD, "popd", builtin_popd)
DEF_BUILTIN(PRINTF, "printf", builtin_printf)
DEF_BUILTIN(DIRS, "dirs", builtin_dirs)
DEF_BUILTIN(EXIT, "exit", builtin_exit)
DEF_BUILTIN(COLON, ":", builtin_colon)
DEF_BUILTIN(TRUE, "true", builtin_true)
DEF_BUILTIN(FALSE, "false", builtin_false)
DEF_BUILTIN(ECHO, "echo", builtin_echo)
DEF_BUILTIN(PWD, "pwd", builtin_pwd)
DEF_BUILTIN(JOBS, "jobs", builtin_jobs)
DEF_BUILTIN(FG, "fg", builtin_fg)
DEF_BUILTIN(BG, "bg", builtin_bg)
DEF_BUILTIN(KILL, "kill", builtin_kill)
DEF_BUILTIN(WAIT, "wait", builtin_wait)
DEF_BUILTIN(EXPORT, "export", builtin_export)
DEF_BUILTIN(READONLY, "readonly", builtin_readonly)
DEF_BUILTIN(LOCAL, "local", builtin_local)
DEF_BUILTIN(UNSET, "unset", builtin_unset)
DEF_BUILTIN(HISTORY, "history", builtin_history)
DEF_BUILTIN(FC, "fc", builtin_fc)
DEF_BUILTIN(HASH, "hash", builtin_hash)
DEF_BUILTIN(ALIAS, "alias", builtin_alias)
DEF_BUILTIN(UNALIAS, "unalias", builtin_unalias)
DEF_BUILTIN(READ, "read", builtin_read)
DEF_BUILTIN(RETURN, "return", builtin_return)
DEF_BUILTIN(BREAK, "break", builtin_break)
DEF_BUILTIN(CONTINUE, "continue", builtin_continue)
DEF_BUILTIN(SHIFT, "shift", builtin_shift)
DEF_BUILTIN(GETOPTS, "getopts", builtin_getopts)
DEF_BUILTIN(LET, "let", builtin_let)
DEF_BUILTIN(SET, "set", builtin_set)
DEF_BUILTIN(TRAP, "trap", builtin_trap)
DEF_BUILTIN(TEST, "test", builtin_test)
DEF_BUILTIN(LBRACKET, "[", builtin_test)
DEF_BUILTIN(DBL_LBRACKET, "[[", builtin_cond)
DEF_BUILTIN(TYPE, "type", builtin_type)
DEF_BUILTIN(COMMAND, "command", builtin_command)
DEF_BUILTIN(EVAL, "eval", builtin_eval)
DEF_BUILTIN(EXEC, "exec", builtin_exec)
DEF_BUILTIN(TIME, "time", builtin_time)
DEF_BUILTIN(TIMES, "times", builtin_times)
DEF_BUILTIN(UMASK, "umask", builtin_umask)
DEF_BUILTIN(ULIMIT, "ulimit", builtin_ulimit)
DEF_BUILTIN(SOURCE, "source", builtin_source)
DEF_BUILTIN(DOT, ".", builtin_source)
DEF_BUILTIN(HELP, "help", builtin_help)
