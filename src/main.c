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
#include "history.h"

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
        PipelineSegment *pipeline = parse_line(line, &background);
        if (!pipeline || !pipeline->argv[0]) {
            free_pipeline(pipeline);
            continue;
        }
        add_history(line);

        if (!pipeline->next && run_builtin(pipeline->argv)) {
            free_pipeline(pipeline);
            continue;
        }

        int seg_count = 0;
        for (PipelineSegment *tmp = pipeline; tmp; tmp = tmp->next) seg_count++;
        pid_t *pids = calloc(seg_count, sizeof(pid_t));
        int i = 0;
        int in_fd = -1;
        PipelineSegment *seg = pipeline;
        int pipefd[2];
        while (seg) {
            if (seg->next && pipe(pipefd) < 0) {
                perror("pipe");
                break;
            }
            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGINT, SIG_DFL);
                if (in_fd != -1) {
                    dup2(in_fd, STDIN_FILENO);
                    close(in_fd);
                }
                if (seg->next) {
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                }
                if (seg->in_file) {
                    int fd = open(seg->in_file, O_RDONLY);
                    if (fd < 0) {
                        perror(seg->in_file);
                        exit(1);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
                if (seg->out_file) {
                    int flags = O_WRONLY | O_CREAT;
                    if (seg->append)
                        flags |= O_APPEND;
                    else
                        flags |= O_TRUNC;
                    int fd = open(seg->out_file, flags, 0644);
                    if (fd < 0) {
                        perror(seg->out_file);
                        exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                execvp(seg->argv[0], seg->argv);
                perror("exec");
                exit(1);
            } else if (pid < 0) {
                perror("fork");
            } else {
                pids[i++] = pid;
                if (in_fd != -1) close(in_fd);
                if (seg->next) {
                    close(pipefd[1]);
                    in_fd = pipefd[0];
                }
            }
            seg = seg->next;
        }
        if (in_fd != -1) close(in_fd);

        if (background) {
            if (i > 0) add_job(pids[i-1], line);
        } else {
            int status;
            for (int j = 0; j < i; j++)
                waitpid(pids[j], &status, 0);
        }
        free(pids);
        free_pipeline(pipeline);
    }
    if (input != stdin)
        fclose(input);
    return 0;
}

