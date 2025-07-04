/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Signal trap management.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "trap.h"
#include "parser.h"
#include "execute.h"
#include "shell_state.h"
#include "builtins.h"
#include "util.h"
#include <unistd.h>


static volatile sig_atomic_t *pending_traps;
static int trap_count;

void init_pending_traps(int count)
{
    trap_count = count;
    pending_traps = xcalloc((size_t)count, sizeof(sig_atomic_t));
}

void free_pending_traps(void)
{
    free((void *)pending_traps);
    pending_traps = NULL;
    trap_count = 0;
}

/* Record that a trapped signal was received. */
void trap_handler(int sig)
{
    if (sig > 0 && sig < trap_count)
        pending_traps[sig] = 1;
}

/* Execute any queued trap commands. Returns the number executed. */
int process_pending_traps(void)
{
    int ran = 0;
    for (int s = 1; s < trap_count; s++) {
        if (pending_traps[s]) {
            pending_traps[s] = 0;
            char *cmd = trap_cmds[s];
            if (!cmd)
                continue;
            FILE *prev = parse_input;
            parse_input = stdin;
            Command *cmds = parse_line(cmd);
            if (cmds) {
                CmdOp prevop = OP_SEMI;
                for (Command *c = cmds; c; c = c->next) {
                    int run = 1;
                    if (c != cmds) {
                        if (prevop == OP_AND)
                            run = (last_status == 0);
                        else if (prevop == OP_OR)
                            run = (last_status != 0);
                    }
                    if (run)
                        run_pipeline(c, cmd);
                    prevop = c->op;
                }
            }
            free_commands(cmds);
            parse_input = prev;
            ran = 1;
        }
    }
    return ran;
}

/* Check if any traps are waiting to be executed. */
int any_pending_traps(void)
{
    for (int s = 1; s < trap_count; s++)
        if (pending_traps[s])
            return 1;
    return 0;
}

/* Execute the command registered for EXIT, if any. */
void run_exit_trap(void)
{
    if (!exit_trap_cmd)
        return;
    FILE *prev = parse_input;
    parse_input = stdin;
    Command *cmds = parse_line(exit_trap_cmd);
    if (cmds) {
        CmdOp prevop = OP_SEMI;
        for (Command *c = cmds; c; c = c->next) {
            int run = 1;
            if (c != cmds) {
                if (prevop == OP_AND)
                    run = (last_status == 0);
                else if (prevop == OP_OR)
                    run = (last_status != 0);
            }
            if (run)
                run_pipeline(c, exit_trap_cmd);
            prevop = c->op;
        }
    }
    free_commands(cmds);
    parse_input = prev;
    free(exit_trap_cmd);
    exit_trap_cmd = NULL;
}

