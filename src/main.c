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

#include "parser.h"
#include "jobs.h"
#include "builtins.h"
#include "history.h"

int main(int argc, char **argv) {
    char line[MAX_LINE];
    char *args[MAX_TOKENS];

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

    while (1) {
        check_jobs();
        if (interactive) {
            printf("vush> ");
            fflush(stdout);
        }
        if (!fgets(line, sizeof(line), input)) break;
        size_t len = strlen(line);
        if (len && line[len-1] == '\n') line[len-1] = '\0';
        int background = 0;
        int ac = parse_line(line, args, &background);
        if (ac == 0) continue;
        add_history(line);
        if (run_builtin(args)) {
            for (int i = 0; i < ac; i++) free(args[i]);
            continue;
        }
        pid_t pid = fork();
        if (pid == 0) {
            /* Restore default SIGINT handling for the child */
            signal(SIGINT, SIG_DFL);
            execvp(args[0], args);
            perror("exec");
            exit(1);
        } else if (pid > 0) {
            if (background) {
                add_job(pid, line);
            } else {
                int status;
                waitpid(pid, &status, 0);
            }
        } else {
            perror("fork");
        }
        for (int i = 0; i < ac; i++) free(args[i]);
    }
    if (input != stdin)
        fclose(input);
    return 0;
}

