/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Initialization routines and config loading.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "startup.h"
#include "parser.h"
#include "history.h"
#include "history_expand.h"
#include "var_expand.h"
#include "execute.h"
#include "util.h"
#include "options.h"


/* Source the specified RC file and execute its commands. */
int process_rc_file(const char *path, FILE *input)
{
    FILE *rc = fopen(path, "r");
    if (!rc)
        return 0;

    int executed = 0;
    char rcline[MAX_LINE];
    while (read_logical_line(rc, rcline, sizeof(rcline))) {
        current_lineno++;
        if (opt_verbose)
            printf("%s\n", rcline);
        char *exp = expand_history(rcline);
        if (!exp)
            continue;
        parse_input = rc;
        Command *cmds = parse_line(exp);
        if (!cmds) {
            free_commands(cmds);
            if (exp != rcline)
                free(exp);
            continue;
        }
        add_history(rcline);
        CmdOp prev = OP_SEMI;
        for (Command *c = cmds; c; c = c->next) {
            int run = 1;
            if (c != cmds) {
                if (prev == OP_AND)
                    run = (last_status == 0);
                else if (prev == OP_OR)
                    run = (last_status != 0);
            }
            if (run)
                run_pipeline(c, exp);
            prev = c->op;
        }
        free_commands(cmds);
        if (exp != rcline)
            free(exp);
        executed = 1;
    }
    fclose(rc);
    parse_input = input;
    return executed;
}

/* Load ~/.vushrc if it exists. */
int process_startup_file(FILE *input)
{
    char *rcpath = make_user_path(NULL, NULL, ".vushrc");
    if (!rcpath) {
        fprintf(stderr, "warning: unable to determine startup file location\n");
        return 0;
    }
    int r = process_rc_file(rcpath, input);
    free(rcpath);
    return r;
}

/* Execute a command provided via -c. */
void run_command_string(const char *cmd)
{
    char linebuf[MAX_LINE];
    strncpy(linebuf, cmd, sizeof(linebuf) - 1);
    linebuf[sizeof(linebuf) - 1] = '\0';
    char *line = linebuf;

    if (opt_verbose)
        printf("%s\n", line);

    char *expanded = expand_history(line);
    if (!expanded)
        return;

    parse_input = stdin;
    Command *cmds = parse_line(expanded);
    if (cmds) {
        add_history(line);
        CmdOp prev = OP_SEMI;
        for (Command *c = cmds; c; c = c->next) {
            int run = 1;
            if (c != cmds) {
                if (prev == OP_AND)
                    run = (last_status == 0);
                else if (prev == OP_OR)
                    run = (last_status != 0);
            }
            if (run)
                run_pipeline(c, expanded);
            prev = c->op;
        }
    }
    free_commands(cmds);
    if (expanded != line)
        free(expanded);
}

