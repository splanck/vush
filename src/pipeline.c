/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Low-level pipeline execution primitives.
 */

/*
 * Low-level pipeline execution primitives.
 *
 * Each pipeline is a linked list of PipelineSegment structures.  The
 * execution engine walks this list and forks a child process for every
 * segment.  When a segment is not the final one, a new pipe is created so
 * that the child's stdout feeds into the next segment's stdin.  The parent
 * retains the read end of that pipe and passes it to the next fork.  In the
 * child process unused pipe ends are closed, redirections are applied and the
 * command is executed.  After all segments have been spawned,
 * wait_for_pipeline() either waits for the children to finish or registers the
 * job for background execution.
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
#include "hash.h"
#include "redir.h"
#include "error.h"


/*
 * Configure the child's standard input and output when spawning a pipeline
 * segment.
 *
 * If 'in_fd' is valid it becomes the child's stdin.  When the segment has a
 * successor, 'pipefd' is the pipe whose write end should be connected to
 * stdout.  All unused pipe ends are closed to avoid descriptor leaks.
 */
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
 * Fork a child process for one pipeline segment.
 *
 * This spawns the command for SEG.  A pipe is created when the segment has a
 * successor.  The child installs the appropriate pipe ends using
 * setup_child_pipes(), applies any I/O redirections and exports temporary
 * assignments before executing the command.  The parent's copy of 'in_fd' is
 * updated with the read end of the pipe so the next segment can consume it.
 */
pid_t fork_segment(PipelineSegment *seg, int *in_fd) {
    if (!seg->argv[0] || seg->argv[0][0] == '\0') {
        fprintf(stderr, "syntax error: missing command\n");
        last_status = 1;
        return -1;
    }

    int pipefd[2];
    if (seg->next)
        RETURN_IF_ERR(pipe(pipefd) < 0, "pipe");

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

        const char *hpath = NULL;
        int hfd = -1;
        if (!strchr(seg->argv[0], '/'))
            hpath = hash_lookup(seg->argv[0], &hfd);
        if (hpath) {
#ifdef HAVE_FEXECVE
            extern char **environ;
            if (hfd >= 0) {
                fcntl(hfd, F_SETFD, 0);
                fexecve(hfd, seg->argv, environ);
            }
#endif
            execv(hpath, seg->argv);
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
 * Wait for all processes spawned for a pipeline.
 *
 * If 'background' is true the last process is recorded in the job list and
 * the function returns immediately.  Otherwise each child's status is
 * collected in order.  The pipefail option is honoured when computing
 * last_status and the shell exits early if opt_errexit is set and the final
 * status is non-zero.
 */
void wait_for_pipeline(pid_t *pids, int count, int background, const char *line) {
    int status = 0;
    int not_found = 0;
    int result = 0;
    if (background) {
        if (count > 0)
            add_job(pids[count - 1], line);
        last_status = 0;
    } else {
        for (int j = 0; j < count; j++) {
            waitpid(pids[j], &status, 0);
            int st;
            if (WIFEXITED(status))
                st = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                st = 128 + WTERMSIG(status);
            else
                st = status;
            if (WIFEXITED(status) && st == 127)
                not_found = 1;
            if (opt_pipefail && st != 0 && result == 0)
                result = st;
        }
        if (!opt_pipefail)
            result = (WIFEXITED(status) ? WEXITSTATUS(status) :
                      (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : status));
        else if (result == 0)
            result = (WIFEXITED(status) ? WEXITSTATUS(status) :
                      (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : status));
        if (not_found)
            result = 127;
        last_status = result;
        if (opt_errexit && last_status != 0)
            exit(last_status);
    }
}

