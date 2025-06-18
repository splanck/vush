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
#include "control.h"

extern int last_status;
extern int param_error;

int loop_break = 0;
int loop_continue = 0;
int loop_depth = 0;

int run_command_list(Command *cmds, const char *line);
static int exec_pipeline(Command *cmd, const char *line);
static int exec_funcdef(Command *cmd, const char *line);

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

