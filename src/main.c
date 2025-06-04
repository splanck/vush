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

#include "parser.h"
#include "jobs.h"
#include "builtins.h"
#include "execute.h"
#include "history.h"

int last_status = 0;

int main(int argc, char **argv) {
    char line[MAX_LINE];

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

    while (1) {
        check_jobs();
        if (interactive) {
            const char *prompt = getenv("PS1");
            if (!prompt) prompt = "vush> ";
            printf("%s", prompt);
            fflush(stdout);
        }
        if (!fgets(line, sizeof(line), input)) break;
        size_t len = strlen(line);
        if (len && line[len-1] == '\n') line[len-1] = '\0';
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
    }
    if (input != stdin)
        fclose(input);
    return 0;
}

