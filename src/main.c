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
#include "scriptargs.h"
#include "options.h"

extern FILE *parse_input;
int last_status = 0;
int script_argc = 0;
char **script_argv = NULL;
int opt_errexit = 0;
int opt_nounset = 0;
int opt_xtrace = 0;

int main(int argc, char **argv) {
    char linebuf[MAX_LINE];
    char *line;

    FILE *input = stdin;
    char *dash_c = NULL;

    if (argc > 1) {
        if (strcmp(argv[1], "-c") == 0) {
            if (argc < 3) {
                fprintf(stderr, "usage: %s -c command\n", argv[0]);
                return 1;
            }
            dash_c = argv[2];
        } else {
            input = fopen(argv[1], "r");
            if (!input) {
                perror(argv[1]);
                return 1;
            }

            script_argc = argc - 2;
            script_argv = calloc(argc, sizeof(char *));
            if (!script_argv) {
                perror("calloc");
                return 1;
            }
            script_argv[0] = argv[1];
            for (int i = 0; i < script_argc; i++)
                script_argv[i + 1] = argv[i + 2];
            script_argv[script_argc + 1] = NULL;
        }
    }

    int interactive = (input == stdin && !dash_c);

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
                    rcline[--len] = '\0';
                while (len > 0) {
                    size_t bs = 0;
                    while (bs < len && rcline[len-1-bs] == '\\')
                        bs++;
                    if (bs % 2 == 1) {
                        rcline[--len] = '\0';
                        char cont[MAX_LINE];
                        if (!fgets(cont, sizeof(cont), rc))
                            break;
                        size_t nlen = strlen(cont);
                        if (nlen && cont[nlen-1] == '\n')
                            cont[--nlen] = '\0';
                        if (len + nlen < sizeof(rcline)) {
                            memcpy(rcline + len, cont, nlen + 1);
                            len += nlen;
                        } else {
                            memcpy(rcline + len, cont, sizeof(rcline) - len - 1);
                            rcline[sizeof(rcline) - 1] = '\0';
                            len = strlen(rcline);
                        }
                    } else {
                        break;
                    }
                }

                char *exp = expand_history(rcline);
                if (!exp)
                    continue;
                parse_input = rc;
                Command *cmds = parse_line(exp);
                if (!cmds || !cmds->pipeline || !cmds->pipeline->argv[0]) {
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
            }
            fclose(rc);
            parse_input = input;
        }
    }

    if (dash_c) {
        strncpy(linebuf, dash_c, sizeof(linebuf) - 1);
        linebuf[sizeof(linebuf) - 1] = '\0';
        line = linebuf;

        char *expanded = expand_history(line);
        if (expanded) {
            parse_input = input;
            Command *cmds = parse_line(expanded);
            if (cmds && cmds->pipeline && cmds->pipeline->argv[0]) {
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
    } else while (1) {
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
            if (len && linebuf[len-1] == '\n')
                linebuf[--len] = '\0';
            while (len > 0) {
                size_t bs = 0;
                while (bs < len && linebuf[len-1-bs] == '\\')
                    bs++;
                if (bs % 2 == 1) {
                    linebuf[--len] = '\0';
                    char cont[MAX_LINE];
                    if (!fgets(cont, sizeof(cont), input))
                        break;
                    size_t nlen = strlen(cont);
                    if (nlen && cont[nlen-1] == '\n')
                        cont[--nlen] = '\0';
                    if (len + nlen < sizeof(linebuf)) {
                        memcpy(linebuf + len, cont, nlen + 1);
                        len += nlen;
                    } else {
                        memcpy(linebuf + len, cont, sizeof(linebuf) - len - 1);
                        linebuf[sizeof(linebuf) - 1] = '\0';
                        len = strlen(linebuf);
                    }
                } else {
                    break;
                }
            }
            line = linebuf;
        }
        char *expanded = expand_history(line);
        if (!expanded) {
            if (line != linebuf)
                free(line);
            continue;
        }
        parse_input = input;
        Command *cmds = parse_line(expanded);
        if (!cmds || !cmds->pipeline || !cmds->pipeline->argv[0]) {
            free_commands(cmds);
            if (expanded != line)
                free(expanded);
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
                run_pipeline(c, expanded);
            prev = c->op;
        }
        free_commands(cmds);
        if (expanded != line)
            free(expanded);
        if (line != linebuf)
            free(line);
    }
    if (input != stdin)
        fclose(input);
    free(script_argv);
    free_aliases();
    return dash_c ? last_status : 0;
}

