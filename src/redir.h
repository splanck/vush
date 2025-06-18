#ifndef REDIR_H
#define REDIR_H

#include "parser.h"

struct redir_save { int in, out, err; };

int apply_redirs_shell(PipelineSegment *seg, struct redir_save *sv);
void restore_redirs_shell(PipelineSegment *seg, struct redir_save *sv);
void redirect_fd(int fd, int dest);
void setup_redirections(PipelineSegment *seg);

#endif /* REDIR_H */
