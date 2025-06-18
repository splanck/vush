/*
 * Execution engine for running parsed command lists.
 *
 * This module drives pipelines, builtins, functions and handles all
 * shell control flow constructs such as loops and conditionals.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>
#include <glob.h>

#include "execute.h"
#include "jobs.h"
#include "builtins.h"
#include "vars.h"
#include "scriptargs.h"
#include "options.h"
#include "pipeline.h"
#include "pipeline_exec.h"
#include "func_exec.h"
#include "arith.h"
#include "util.h"
#include "hash.h"
#include "lexer.h"
#include "redir.h"
#include "assignment_utils.h"

extern int last_status;
extern int param_error;

int loop_break = 0;
int loop_continue = 0;
int loop_depth = 0;

int run_command_list(Command *cmds, const char *line);
static int exec_pipeline(Command *cmd, const char *line);
static int exec_funcdef(Command *cmd, const char *line);
static int exec_if(Command *cmd, const char *line);
static int exec_while(Command *cmd, const char *line);
static int exec_until(Command *cmd, const char *line);
static int exec_for(Command *cmd, const char *line);
static int exec_select(Command *cmd, const char *line);
static int exec_for_arith(Command *cmd, const char *line);
static int exec_case(Command *cmd, const char *line);
static int exec_subshell(Command *cmd, const char *line);
static int exec_cond(Command *cmd, const char *line);
static int exec_group(Command *cmd, const char *line);
static int exec_arith(Command *cmd, const char *line);

int run_command_list(Command *cmds, const char *line) {
    CmdOp prev = OP_SEMI;
    for (Command *c = cmds; c; c = c->next) {
        int run = 1;
        if (c != cmds) {
            if (prev == OP_AND)
                run = (last_status == 0);
            else if (prev == OP_OR)
                run = (last_status != 0);
        }
        if (run) {
            if (opt_noexec) {
                if (c->type == CMD_PIPELINE && c->pipeline &&
                    c->pipeline->argv[0] &&
                    strcmp(c->pipeline->argv[0], "set") == 0)
                    run_pipeline(c, line);
            } else {
                run_pipeline(c, line);
            }
        }
        prev = c->op;
        if (func_return || loop_break || loop_continue)
            break;
    }
    if (loop_depth == 0) {
        loop_break = 0;
        loop_continue = 0;
    }
    return last_status;
}

/*
 * Execute a simple or composed pipeline.  This wraps
 * run_pipeline_internal() and returns its resulting status.
 */
static int exec_pipeline(Command *cmd, const char *line) {
    if (cmd->time_pipeline)
        return run_pipeline_timed(cmd->pipeline, cmd->background, line);
    else
        return run_pipeline_internal(cmd->pipeline, cmd->background, line);
}

/*
 * Define a shell function.  The body of the command becomes the new
 * function definition and last_status is preserved.
 */
static int exec_funcdef(Command *cmd, const char *line) {
    (void)line;
    define_function(cmd->var, NULL, cmd->text);
    free_commands(cmd->body);
    cmd->body = NULL;
    return last_status;
}

/*
 * Execute an if/else construct.  The condition list is run first and
 * the body or else part is selected based on its exit status.  Returns
 * the status of the last command executed.
 */
static int exec_if(Command *cmd, const char *line) {
    run_command_list(cmd->cond, line);
    if (last_status == 0)
        run_command_list(cmd->body, line);
    else if (cmd->else_part)
        run_command_list(cmd->else_part, line);
    return last_status;
}

/*
 * Execute a while loop.  The condition list is repeatedly evaluated
 * and the body executed while it returns success.  Loop control flags
 * loop_break and loop_continue are honored.  Returns last_status.
 */
static int exec_while(Command *cmd, const char *line) {
    loop_depth++;
    while (1) {
        run_command_list(cmd->cond, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
        if (last_status != 0)
            break;
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute an until loop.  This is the inverse of a while loop: the
 * body runs repeatedly until the condition list succeeds.  Loop
 * control flags are processed and the final last_status is returned.
 */
static int exec_until(Command *cmd, const char *line) {
    loop_depth++;
    while (1) {
        run_command_list(cmd->cond, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
        if (last_status == 0)
            break;
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute a for loop over a list of words.  The variable specified by
 * cmd->var is set to each word in turn before the body is executed.
 * Returns last_status from the final iteration.
 */
static int exec_for(Command *cmd, const char *line) {
    loop_depth++;
    for (int i = 0; i < cmd->word_count; i++) {
        char *exp = expand_var(cmd->words[i]);
        if (!exp) { loop_depth--; return last_status; }
        int count = 0;
        char **fields = split_fields(exp, &count);
        free(exp);
        for (int fi = 0; fi < count; fi++) {
            char *w = fields[fi];
            if (cmd->var) {
                set_shell_var(cmd->var, w);
                setenv(cmd->var, w, 1);
            }
            run_command_list(cmd->body, line);
            if (loop_break) break;
            if (loop_continue) {
                if (--loop_continue) {
                    for (int fj = fi; fj < count; fj++)
                        free(fields[fj]);
                    free(fields);
                    loop_depth--;
                    return last_status;
                }
                continue;
            }
        }
        for (int fj = 0; fj < count; fj++)
            free(fields[fj]);
        free(fields);
        if (loop_break) { loop_break--; break; }
    }
    loop_depth--;
    return last_status;
}

/*
 * Implement the POSIX select loop.  A numbered menu is printed for the
 * supplied words and user input chooses which value is assigned to the
 * iteration variable.  The function returns the status of the body.
 */
static int exec_select(Command *cmd, const char *line) {
    loop_depth++;
    char input[MAX_LINE];
    while (1) {
        for (int i = 0; i < cmd->word_count; i++)
            printf("%d) %s\n", i + 1, cmd->words[i]);
        const char *ps3 = getenv("PS3");
        fputs(ps3 ? ps3 : "? ", stdout);
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin))
            break;
        int choice = atoi(input);
        if (choice < 1 || choice > cmd->word_count) {
            continue;
        }
        if (cmd->var)
            setenv(cmd->var, cmd->words[choice - 1], 1);
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute a C-style for loop using arithmetic expressions.  The init,
 * condition and update parts are evaluated with eval_arith().  The loop
 * terminates when the condition evaluates to zero.  Returns last_status.
 */
static int exec_for_arith(Command *cmd, const char *line) {
    (void)line;
    int err = 0;
    char *msg = NULL;
    loop_depth++;

    eval_arith(cmd->arith_init ? cmd->arith_init : "0", &err, &msg);
    if (err) {
        if (msg) {
            fprintf(stderr, "arith: %s\n", msg);
            free(msg);
            msg = NULL;
        }
        last_status = 1;
        loop_depth--;
        return last_status;
    }

    while (1) {
        err = 0;
        long cond = eval_arith(cmd->arith_cond ? cmd->arith_cond : "1", &err, &msg);
        if (err) { 
            if (msg) {
                fprintf(stderr, "arith: %s\n", msg);
                free(msg);
                msg = NULL;
            }
            last_status = 1; break; }
        if (cond == 0)
            break;

        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            err = 0;
            eval_arith(cmd->arith_update ? cmd->arith_update : "0", &err, &msg);
            if (err) { 
                if (msg) {
                    fprintf(stderr, "arith: %s\n", msg);
                    free(msg);
                    msg = NULL;
                }
                last_status = 1; break; }
            continue;
        }

        err = 0;
        eval_arith(cmd->arith_update ? cmd->arith_update : "0", &err, &msg);
        if (err) { 
            if (msg) {
                fprintf(stderr, "arith: %s\n", msg);
                free(msg);
                msg = NULL;
            }
            last_status = 1; break; }
    }

    loop_depth--;
    return last_status;
}

/*
 * Execute a case statement.  Each pattern list is matched against the
 * supplied word using fnmatch().  The body of the first matching case
 * is executed and last_status from that body is returned.
 */
static int exec_case(Command *cmd, const char *line) {
    int fall = 0;
    for (CaseItem *ci = cmd->cases; ci; ci = ci->next) {
        if (fall) {
            run_command_list(ci->body, line);
            if (!ci->fall_through)
                break;
            fall = ci->fall_through;
            continue;
        }

        int matched = 0;
        for (int i = 0; i < ci->pattern_count; i++) {
            if (fnmatch(ci->patterns[i], cmd->var, 0) == 0) {
                matched = 1;
                break;
            }
        }
        if (matched) {
            run_command_list(ci->body, line);
            if (!ci->fall_through)
                break;
            fall = ci->fall_through;
        }
    }
    return last_status;
}

/*
 * Spawn a subshell running the provided command group.  The parent waits
 * for the child to finish and propagates its exit status.  Returns that
 * status or 1 on fork failure.
 */
static int exec_subshell(Command *cmd, const char *line) {
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        run_command_list(cmd->group, line);
        exit(last_status);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            last_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            last_status = 128 + WTERMSIG(status);
        return last_status;
    } else {
        perror("fork");
        last_status = 1;
        return 1;
    }
}

/*
 * Execute the [[ ... ]] conditional command.  This simply forwards to
 * builtin_cond() and returns its result.
 */
static int exec_cond(Command *cmd, const char *line) {
    (void)line;
    char **args = xcalloc(cmd->word_count + 1, sizeof(char *));
    for (int i = 0; i < cmd->word_count; i++) {
        args[i] = expand_var(cmd->words[i]);
        if (!args[i]) {
            for (int j = 0; j < i; j++)
                free(args[j]);
            free(args);
            last_status = 1;
            return 1;
        }
    }
    args[cmd->word_count] = NULL;
    builtin_cond(args);
    for (int i = 0; i < cmd->word_count; i++)
        free(args[i]);
    free(args);
    return last_status;
}

/*
 * Execute a (( ... )) arithmetic command. The expression is evaluated with
 * eval_arith() and the exit status becomes 0 for a non-zero result or 1
 * otherwise.
 */
static int exec_arith(Command *cmd, const char *line) {
    (void)line;
    int err = 0;
    char *msg = NULL;
    long val = eval_arith(cmd->text ? cmd->text : "0", &err, &msg);
    if (err) {
        if (msg) {
            fprintf(stderr, "arith: %s\n", msg);
            free(msg);
        }
        last_status = 1;
    }
    else
        last_status = (val != 0) ? 0 : 1;
    return last_status;
}

/*
 * Execute a { ... } command group in the current shell.  Simply runs
 * the contained command list and returns its status.
 */
static int exec_group(Command *cmd, const char *line) {
    run_command_list(cmd->group, line);
    return last_status;
}

int run_pipeline(Command *cmd, const char *line) {
    if (!cmd)
        return 0;
    if (opt_hashall && cmd->type == CMD_PIPELINE) {
        for (PipelineSegment *seg = cmd->pipeline; seg; seg = seg->next) {
            if (seg->argv[0] && !strchr(seg->argv[0], '/'))
                hash_add(seg->argv[0]);
        }
    }
    int r = 0;
    switch (cmd->type) {
    case CMD_PIPELINE:
        r = exec_pipeline(cmd, line); break;
    case CMD_FUNCDEF:
        r = exec_funcdef(cmd, line); break;
    case CMD_IF:
        r = exec_if(cmd, line); break;
    case CMD_WHILE:
        r = exec_while(cmd, line); break;
    case CMD_UNTIL:
        r = exec_until(cmd, line); break;
    case CMD_FOR:
        r = exec_for(cmd, line); break;
    case CMD_SELECT:
        r = exec_select(cmd, line); break;
    case CMD_FOR_ARITH:
        r = exec_for_arith(cmd, line); break;
    case CMD_CASE:
        r = exec_case(cmd, line); break;
    case CMD_SUBSHELL:
        r = exec_subshell(cmd, line); break;
    case CMD_COND:
        r = exec_cond(cmd, line); break;
    case CMD_ARITH:
        r = exec_arith(cmd, line); break;
    case CMD_GROUP:
        r = exec_group(cmd, line); break;
    default:
        r = 0; break;
    }
    if (cmd->negate) {
        last_status = (last_status == 0 ? 1 : 0);
        r = last_status;
    }
    return r;
}

