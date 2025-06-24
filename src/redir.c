/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Redirection helpers.
 */

/*
 * Redirection helpers for builtins and child processes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "redir.h"
#include "util.h"

/* Apply redirections in the current shell process and
 * save the original descriptors in SV for restoration. */
int apply_redirs_shell(PipelineSegment *seg, struct redir_save *sv) {
    sv->in = sv->out = sv->err = -1;
    if (seg->in_file) {
        sv->in = dup(seg->in_fd);
        int fd = open(seg->in_file, O_RDONLY);
        if (fd < 0) {
            perror(seg->in_file);
            return -1;
        }
        if (seg->here_doc)
            unlink(seg->in_file);
        dup2(fd, seg->in_fd);
        close(fd);
    }

    if (seg->out_file && seg->err_file && strcmp(seg->out_file, seg->err_file) == 0 &&
        seg->append == seg->err_append) {
        sv->out = dup(seg->out_fd);
        sv->err = dup(STDERR_FILENO);
        int fd = open_redirect(seg->out_file, seg->append, seg->force);
        if (fd < 0) {
            perror(seg->out_file);
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    } else {
        if (seg->out_file) {
            sv->out = dup(seg->out_fd);
            int fd = open_redirect(seg->out_file, seg->append, seg->force);
            if (fd < 0) {
                perror(seg->out_file);
                return -1;
            }
            dup2(fd, seg->out_fd);
            close(fd);
        }
        if (seg->err_file) {
            sv->err = dup(STDERR_FILENO);
            int fd = open_redirect(seg->err_file, seg->err_append, 0);
            if (fd < 0) {
                perror(seg->err_file);
                return -1;
            }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    }

    if (seg->close_out) {
        if (sv->out == -1)
            sv->out = dup(seg->out_fd);
        close(seg->out_fd);
    } else if (seg->dup_out != -1) {
        if (sv->out == -1)
            sv->out = dup(seg->out_fd);
        dup2(seg->dup_out, seg->out_fd);
    }

    if (seg->close_err) {
        if (sv->err == -1)
            sv->err = dup(STDERR_FILENO);
        close(STDERR_FILENO);
    } else if (seg->dup_err != -1) {
        if (sv->err == -1)
            sv->err = dup(STDERR_FILENO);
        dup2(seg->dup_err, STDERR_FILENO);
    }

    return 0;
}

/* Restore shell redirections saved by apply_redirs_shell(). */
void restore_redirs_shell(PipelineSegment *seg, struct redir_save *sv) {
    if (sv->in != -1) { dup2(sv->in, seg->in_fd); close(sv->in); }
    if (sv->out != -1) { dup2(sv->out, seg->out_fd); close(sv->out); }
    if (sv->err != -1) { dup2(sv->err, STDERR_FILENO); close(sv->err); }
}

/* Duplicate FD onto DEST and close FD. */
void redirect_fd(int fd, int dest) {
    dup2(fd, dest);
    close(fd);
}

/* Apply pending redirections in a child process. */
void setup_redirections(PipelineSegment *seg) {
    if (seg->in_file) {
        int fd = open(seg->in_file, O_RDONLY);
        if (fd < 0) {
            perror(seg->in_file);
            exit(1);
        }
        if (seg->here_doc)
            unlink(seg->in_file);
        redirect_fd(fd, seg->in_fd);
    }

    if (seg->out_file && seg->err_file && strcmp(seg->out_file, seg->err_file) == 0 &&
        seg->append == seg->err_append) {
        int fd = open_redirect(seg->out_file, seg->append, seg->force);
        if (fd < 0) {
            perror(seg->out_file);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    } else {
        if (seg->out_file) {
            int fd = open_redirect(seg->out_file, seg->append, seg->force);
            if (fd < 0) {
                perror(seg->out_file);
                exit(1);
            }
            redirect_fd(fd, seg->out_fd);
        }
        if (seg->err_file) {
            int fd = open_redirect(seg->err_file, seg->err_append, 0);
            if (fd < 0) {
                perror(seg->err_file);
                exit(1);
            }
            redirect_fd(fd, STDERR_FILENO);
        }
    }

    if (seg->close_out) {
        close(seg->out_fd);
        if (seg->out_fd == STDERR_FILENO)
            seg->close_err = 0;
        if (seg->dup_err == seg->out_fd)
            seg->dup_err = -1;
    } else if (seg->dup_out != -1) {
        dup2(seg->dup_out, seg->out_fd);
    }

    if (seg->close_err)
        close(STDERR_FILENO);
    else if (seg->dup_err != -1)
        dup2(seg->dup_err, STDERR_FILENO);
}
