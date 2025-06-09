/*
 * Low-level pipeline execution primitives.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#include "pipeline.h"
#include "jobs.h"
#include "options.h"
extern int last_status;

extern void setup_redirections(PipelineSegment *seg);

void setup_child_pipes(PipelineSegment *seg, int in_fd, int pipefd[2]) {
    if (in_fd != -1) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }
    if (seg->next) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
    }
}

/*
 * Fork a process for one segment of the pipeline and set up pipes
 * and redirections before executing the command.
 */
pid_t fork_segment(PipelineSegment *seg, int *in_fd) {
    if (!seg->argv[0]) {
        fprintf(stderr, "syntax error: missing command\n");
        last_status = 1;
        return -1;
    }

    int pipefd[2];
    if (seg->next && pipe(pipefd) < 0) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        setup_child_pipes(seg, *in_fd, pipefd);
        setup_redirections(seg);

        for (int ai = 0; ai < seg->assign_count; ai++) {
            char *eq = strchr(seg->assigns[ai], '=');
            if (eq) {
                size_t len = (size_t)(eq - seg->assigns[ai]);
                char *name = strndup(seg->assigns[ai], len);
                if (name) {
                    setenv(name, eq + 1, 1);
                    free(name);
                }
            }
        }

        execvp(seg->argv[0], seg->argv);
        if (errno == ENOENT)
            fprintf(stderr, "%s: command not found\n", seg->argv[0]);
        else
            fprintf(stderr, "%s: %s\n", seg->argv[0], strerror(errno));
        exit(127);
    } else if (pid > 0) {
        if (*in_fd != -1)
            close(*in_fd);
        if (seg->next) {
            close(pipefd[1]);
            *in_fd = pipefd[0];
        } else {
            *in_fd = -1;
        }
    } else {
        perror("fork");
        if (seg->next) {
            close(pipefd[0]);
            close(pipefd[1]);
        }
    }
    return pid;
}

/*
 * Wait for all pipeline processes to finish or add the job to the
 * background list when requested.
 */
void wait_for_pipeline(pid_t *pids, int count, int background, const char *line) {
    int status = 0;
    if (background) {
        if (count > 0)
            add_job(pids[count - 1], line);
        last_status = 0;
    } else {
        for (int j = 0; j < count; j++)
            waitpid(pids[j], &status, 0);
        if (WIFEXITED(status))
            last_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            last_status = 128 + WTERMSIG(status);
        else
            last_status = status;
        if (opt_errexit && last_status != 0)
            exit(last_status);
    }
}

