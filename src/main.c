/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "common.h"

#include "parser.h"
#include "jobs.h"
#include "builtins.h"
#include "execute.h"
#include "history.h"
#include "lineedit.h"

int last_status = 0;

int main(int argc, char **argv) {
    char linebuf[MAX_LINE];
    char *line;

    FILE *input = stdin;
    if (argc > 1) {
        input = fopen(argv[1], "r");
        if (!input) {
            perror(argv[1]);
            return 1;
        }
    }

    int interactive = (input == stdin);

    /* Ignore Ctrl-C in the shell itself */
    signal(SIGINT, SIG_IGN);

    load_history();
    load_aliases();

    /* Execute commands from ~/.vushrc if present */
    const char *home = getenv("HOME");
    if (home) {
        char rcpath[PATH_MAX];
        snprintf(rcpath, sizeof(rcpath), "%s/.vushrc", home);
        FILE *rc = fopen(rcpath, "r");
        if (rc) {
            char rcline[MAX_LINE];
            while (fgets(rcline, sizeof(rcline), rc)) {
                size_t len = strlen(rcline);
                if (len && rcline[len-1] == '\n')
                    rcline[len-1] = '\0';

                Command *cmds = parse_line(rcline);
                if (!cmds || !cmds->pipeline || !cmds->pipeline->argv[0]) {
                    free_commands(cmds);
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
                        run_pipeline(c->pipeline, c->background, rcline);
                    prev = c->op;
                }
                free_commands(cmds);
            }
            fclose(rc);
        }
    }

    while (1) {
        check_jobs();
        if (interactive) {
            const char *ps = getenv("PS1");
            char *prompt = expand_prompt(ps ? ps : "vush> ");
            history_reset_cursor();
            line = line_edit(prompt);
            free(prompt);
            if (!line) break;
        } else {
            if (!fgets(linebuf, sizeof(linebuf), input)) break;
            size_t len = strlen(linebuf);
            if (len && linebuf[len-1] == '\n') linebuf[len-1] = '\0';
            line = linebuf;
        }
        Command *cmds = parse_line(line);
        if (!cmds || !cmds->pipeline || !cmds->pipeline->argv[0]) {
            free_commands(cmds);
            continue;
        }
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
                run_pipeline(c->pipeline, c->background, line);
            prev = c->op;
        }
        free_commands(cmds);
        if (line != linebuf)
            free(line);
    }
    if (input != stdin)
        fclose(input);
    free_aliases();
    return 0;
}

