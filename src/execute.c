#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include "execute.h"
#include "jobs.h"
#include "builtins.h"

int run_pipeline(PipelineSegment *pipeline, int background, const char *line) {
    if (!pipeline)
        return 0;

    if (!pipeline->next && run_builtin(pipeline->argv))
        return 0;

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
            free(pids);
            return 1;
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

    int status = 0;
    if (background) {
        if (i > 0)
            add_job(pids[i-1], line);
    } else {
        for (int j = 0; j < i; j++)
            waitpid(pids[j], &status, 0);
    }
    free(pids);
    return WEXITSTATUS(status);
}
