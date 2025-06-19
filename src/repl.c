#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "repl.h"
#include "parser.h"
#include "execute.h"
#include "history.h"
#include "history_expand.h"
#include "prompt_expand.h"
#include "lineedit.h"
#include "jobs.h"
#include "trap.h"
#include "mail.h"
#include "util.h"
#include "options.h"
#include "vars.h"


void repl_loop(FILE *input)
{
    char linebuf[MAX_LINE];
    char *line;
    int interactive = (input == stdin);
    int eof_count = 0;

    while (1) {
        process_pending_traps();
        if (opt_monitor)
            check_jobs();
        else
            while (waitpid(-1, NULL, WNOHANG) > 0)
                ;
        if (interactive) {
            check_mail();
            const char *ps = get_shell_var("PS1");
            if (!ps)
                ps = getenv("PS1");
            char *prompt = expand_prompt(ps ? ps : "vush> ");
            jobs_at_prompt = 1;
            check_jobs();
            if (jobs_at_prompt)
                line = line_edit(prompt);
            else
                line = line_edit("");
            jobs_at_prompt = 0;
            free(prompt);
            if (!line) {
                if (any_pending_traps()) {
                    if (interactive)
                        printf("\n");
                    process_pending_traps();
                    continue;
                }
                if (opt_ignoreeof) {
                    eof_count++;
                    if (eof_count < 10) {
                        printf("\nUse \"exit\" to leave the shell.\n");
                        continue;
                    }
                }
                break;
            }
            eof_count = 0;
            current_lineno++;
        } else {
            if (!read_logical_line(input, linebuf, sizeof(linebuf))) {
                if (process_pending_traps())
                    continue;
                break;
            }
            current_lineno++;
            line = linebuf;
        }

        if (opt_verbose)
            printf("%s\n", line);

        char *cmdline = strdup(line);
        if (line != linebuf)
            free(line);
        if (!cmdline) {
            perror("strdup");
            break;
        }

        while (1) {
            char *expanded = expand_history(cmdline);
            if (!expanded) {
                free(cmdline);
                cmdline = NULL;
                break;
            }

            parse_input = input;
            Command *cmds = parse_line(expanded);
            if (parse_need_more) {
                free_commands(cmds);
                free(expanded);
                const char *ps2 = getenv("PS2");
                char *more = NULL;
                if (interactive) {
                    char *p2 = expand_prompt(ps2 ? ps2 : "> ");
                    jobs_at_prompt = 1;
                    more = line_edit(p2);
                    jobs_at_prompt = 0;
                    free(p2);
                    if (!more) {
                        free(cmdline);
                        cmdline = NULL;
                        if (any_pending_traps()) {
                            printf("\n");
                            process_pending_traps();
                        }
                        break;
                    }
                    current_lineno++;
                } else {
                    if (!read_logical_line(input, linebuf, sizeof(linebuf))) {
                        free(cmdline);
                        cmdline = NULL;
                        if (any_pending_traps())
                            process_pending_traps();
                        break;
                    }
                    current_lineno++;
                    more = strdup(linebuf);
                }
                if (opt_verbose)
                    printf("%s\n", more);
                size_t len1 = strlen(cmdline);
                size_t len2 = strlen(more);
                char *tmp = malloc(len1 + len2 + 2);
                if (!tmp) {
                    perror("malloc");
                    free(cmdline);
                    free(more);
                    cmdline = NULL;
                    break;
                }
                memcpy(tmp, cmdline, len1);
                tmp[len1] = '\n';
                memcpy(tmp + len1 + 1, more, len2 + 1);
                free(cmdline);
                free(more);
                cmdline = tmp;
                continue;
            }

            if (!cmds) {
                if (feof(input))
                    clearerr(input);
                free_commands(cmds);
                free(expanded);
                break;
            }

            add_history(cmdline);

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
            free_commands(cmds);
            free(expanded);
            process_pending_traps();
            break;
        }

        free(cmdline);
        if (opt_onecmd)
            break;
    }
}

