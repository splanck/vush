#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "execute.h"
#include "jobs.h"
#include "builtins.h"
#include "scriptargs.h"
#include "options.h"

extern int last_status;
int func_return = 0;

static int run_command_list(Command *cmds, const char *line);
static int run_function(Command *body, char **args);
static int apply_temp_assignments(PipelineSegment *pipeline);
static void setup_redirections(PipelineSegment *seg);
static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line);

static int apply_temp_assignments(PipelineSegment *pipeline) {
    if (pipeline->next)
        return 0;

    if (!pipeline->argv[0] && pipeline->assign_count > 0) {
        for (int i = 0; i < pipeline->assign_count; i++) {
            char *eq = strchr(pipeline->assigns[i], '=');
            if (!eq)
                continue;
            char *name = strndup(pipeline->assigns[i], eq - pipeline->assigns[i]);
            if (name) {
                set_shell_var(name, eq + 1);
                free(name);
            }
        }
        last_status = 0;
        return 1;
    }

    if (!pipeline->argv[0])
        return 0;

    struct {
        char *name;
        char *env;
        char *var;
        int had_env;
        int had_var;
    } backs[pipeline->assign_count];

    for (int i = 0; i < pipeline->assign_count; i++) {
        char *eq = strchr(pipeline->assigns[i], '=');
        if (!eq) {
            backs[i].name = NULL;
            continue;
        }
        backs[i].name = strndup(pipeline->assigns[i], eq - pipeline->assigns[i]);
        const char *oe = getenv(backs[i].name);
        backs[i].had_env = oe != NULL;
        backs[i].env = oe ? strdup(oe) : NULL;
        const char *ov = get_shell_var(backs[i].name);
        backs[i].had_var = ov != NULL;
        backs[i].var = ov ? strdup(ov) : NULL;
        setenv(backs[i].name, eq + 1, 1);
        set_shell_var(backs[i].name, eq + 1);
    }

    int handled = 0;
    if (run_builtin(pipeline->argv))
        handled = 1;
    else {
        Command *fn = get_function(pipeline->argv[0]);
        if (fn) {
            run_function(fn, pipeline->argv);
            handled = 1;
        }
    }

    for (int i = 0; i < pipeline->assign_count; i++) {
        if (!backs[i].name)
            continue;
        if (backs[i].had_env)
            setenv(backs[i].name, backs[i].env, 1);
        else
            unsetenv(backs[i].name);
        if (backs[i].had_var)
            set_shell_var(backs[i].name, backs[i].var);
        else
            unset_shell_var(backs[i].name);
        free(backs[i].name);
        free(backs[i].env);
        free(backs[i].var);
    }

    return handled;
}

static void setup_redirections(PipelineSegment *seg) {
    if (seg->in_file) {
        int fd = open(seg->in_file, O_RDONLY);
        if (fd < 0) {
            perror(seg->in_file);
            exit(1);
        }
        if (seg->here_doc)
            unlink(seg->in_file);
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (seg->out_file && seg->err_file && strcmp(seg->out_file, seg->err_file) == 0 &&
        seg->append == seg->err_append) {
        int flags = O_WRONLY | O_CREAT | (seg->append ? O_APPEND : O_TRUNC);
        int fd = open(seg->out_file, flags, 0644);
        if (fd < 0) {
            perror(seg->out_file);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    } else {
        if (seg->out_file) {
            int flags = O_WRONLY | O_CREAT | (seg->append ? O_APPEND : O_TRUNC);
            int fd = open(seg->out_file, flags, 0644);
            if (fd < 0) {
                perror(seg->out_file);
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (seg->err_file) {
            int flags = O_WRONLY | O_CREAT | (seg->err_append ? O_APPEND : O_TRUNC);
            int fd = open(seg->err_file, flags, 0644);
            if (fd < 0) {
                perror(seg->err_file);
                exit(1);
            }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    }

    if (seg->dup_out != -1)
        dup2(seg->dup_out, STDOUT_FILENO);
    if (seg->dup_err != -1)
        dup2(seg->dup_err, STDERR_FILENO);
}

static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line) {
    int seg_count = 0;
    for (PipelineSegment *tmp = pipeline; tmp; tmp = tmp->next)
        seg_count++;
    pid_t *pids = calloc(seg_count, sizeof(pid_t));
    if (!pids)
        return 1;

    int i = 0;
    int in_fd = -1;
    int pipefd[2];

    for (PipelineSegment *seg = pipeline; seg; seg = seg->next) {
        if (seg->next && pipe(pipefd) < 0) {
            perror("pipe");
            free(pids);
            last_status = 1;
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
        } else if (pid < 0) {
            perror("fork");
        } else {
            pids[i++] = pid;
            if (in_fd != -1)
                close(in_fd);
            if (seg->next) {
                close(pipefd[1]);
                in_fd = pipefd[0];
            }
        }
    }

    if (in_fd != -1)
        close(in_fd);

    int status = 0;
    if (background) {
        if (i > 0)
            add_job(pids[i - 1], line);
        last_status = 0;
    } else {
        for (int j = 0; j < i; j++)
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

    free(pids);
    return last_status;
}

static int run_pipeline_internal(PipelineSegment *pipeline, int background, const char *line) {
    if (!pipeline)
        return 0;

    if (opt_xtrace && line)
        fprintf(stderr, "+ %s\n", line);

    if (apply_temp_assignments(pipeline))
        return last_status;

    return spawn_pipeline_segments(pipeline, background, line);
}

static int run_command_list(Command *cmds, const char *line) {
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
            run_pipeline(c, line);
        prev = c->op;
        if (func_return)
            break;
    }
    return last_status;
}

int run_pipeline(Command *cmd, const char *line) {
    if (!cmd)
        return 0;

    switch (cmd->type) {
    case CMD_PIPELINE:
        return run_pipeline_internal(cmd->pipeline, cmd->background, line);
    case CMD_FUNCDEF:
        define_function(cmd->var, cmd->body, cmd->text);
        cmd->body = NULL;
        return last_status;
    case CMD_IF:
        run_command_list(cmd->cond, line);
        if (last_status == 0)
            run_command_list(cmd->body, line);
        else if (cmd->else_part)
            run_command_list(cmd->else_part, line);
        return last_status;
    case CMD_WHILE:
        while (1) {
            run_command_list(cmd->cond, line);
            if (last_status != 0)
                break;
            run_command_list(cmd->body, line);
        }
        return last_status;
    case CMD_FOR:
        for (int i = 0; i < cmd->word_count; i++) {
            if (cmd->var)
                setenv(cmd->var, cmd->words[i], 1);
            run_command_list(cmd->body, line);
        }
        return last_status;
    default:
        return 0;
    }
}

static int run_function(Command *body, char **args) {
    int argc = 0;
    while (args[argc]) argc++;
    int old_argc = script_argc;
    char **old_argv = script_argv;
    script_argc = argc - 1;
    script_argv = calloc(argc + 1, sizeof(char *));
    for (int i = 0; i < argc; i++)
        script_argv[i] = strdup(args[i]);
    script_argv[argc] = NULL;
    func_return = 0;
    run_command_list(body, NULL);
    for (int i = 0; i < argc; i++)
        free(script_argv[i]);
    free(script_argv);
    script_argv = old_argv;
    script_argc = old_argc;
    return last_status;
}
