/*
 * Execution engine for running parsed command lists.
 *
 * This module drives pipelines, builtins, functions and handles all
 * shell control flow constructs such as loops and conditionals.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>

#include "execute.h"
#include "jobs.h"
#include "builtins.h"
#include "vars.h"
#include "scriptargs.h"
#include "options.h"
#include "pipeline.h"
#include "func_exec.h"
#include "arith.h"
#include "util.h"
#include "hash.h"

extern int last_status;

int loop_break = 0;
int loop_continue = 0;
int loop_depth = 0;

int run_command_list(Command *cmds, const char *line);
static int apply_temp_assignments(PipelineSegment *pipeline);
void setup_redirections(PipelineSegment *seg);
static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line);
static int exec_pipeline(Command *cmd, const char *line);
static int exec_funcdef(Command *cmd, const char *line);
static int exec_if(Command *cmd, const char *line);
static int exec_while(Command *cmd, const char *line);
static int exec_until(Command *cmd, const char *line);
static int exec_for(Command *cmd, const char *line);
static int exec_select(Command *cmd, const char *line);
static int exec_for_arith(Command *cmd, const char *line);
static int exec_case(Command *cmd, const char *line);
static int exec_subshell(Command *cmd, const char *line);
static int exec_cond(Command *cmd, const char *line);
static int exec_group(Command *cmd, const char *line);

/*
 * Duplicate the given descriptor onto another descriptor and close the
 * original.  Used internally when applying redirections.  Does not
 * modify last_status.
 */
static void redirect_fd(int fd, int dest) {
    dup2(fd, dest);
    close(fd);
}
/*
 * Apply temporary variable assignments before running a pipeline.
 * Builtins and functions are executed directly while environment
 * variables are preserved and restored around the call.
 */

static int apply_temp_assignments(PipelineSegment *pipeline) {
    if (pipeline->next)
        return 0;

    if (!pipeline->argv[0] && pipeline->assign_count > 0) {
        for (int i = 0; i < pipeline->assign_count; i++) {
            char *eq = strchr(pipeline->assigns[i], '=');
            if (!eq)
                continue;
            char *name = strndup(pipeline->assigns[i], eq - pipeline->assigns[i]);
            if (!name)
                continue;
            char *val = eq + 1;
            size_t vlen = strlen(val);
            if (vlen > 1 && val[0] == '(' && val[vlen-1] == ')') {
                char *body = strndup(val+1, vlen-2);
                if (!body) {
                    free(name);
                    continue;
                }
                char *p = body;
                char **vals = NULL; int count=0; int failed = 0;
                while (*p) {
                    while (*p==' '||*p=='\t') p++;
                    if (*p=='\0') break;
                    char *start=p;
                    while (*p && *p!=' ' && *p!='\t') p++;
                    if (*p) *p++='\0';
                    char **tmp = realloc(vals, sizeof(char*)*(count+1));
                    if (!tmp) {
                        failed = 1;
                        break;
                    }
                    vals = tmp;
                    vals[count] = strdup(start);
                    if (!vals[count]) {
                        failed = 1;
                        break;
                    }
                    count++;
                }
                if (!failed) {
                    set_shell_array(name, vals, count);
                    if (opt_allexport) {
                        size_t joinlen = 0;
                        for (int j=0;j<count;j++)
                            joinlen += strlen(vals[j]) + 1;
                        char *joined = malloc(joinlen+1);
                        if (joined) {
                            joined[0] = '\0';
                            for (int j=0;j<count;j++) {
                                strcat(joined, vals[j]);
                                if (j < count-1) strcat(joined, " ");
                            }
                            setenv(name, joined, 1);
                            free(joined);
                        }
                    }
                }
                for(int j=0;j<count;j++) free(vals[j]);
                free(vals);
                free(body);
            } else {
                set_shell_var(name, val);
                if (opt_allexport)
                    setenv(name, val, 1);
            }
            free(name);
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
        if (!backs[i].name) {
            backs[i].env = backs[i].var = NULL;
            backs[i].had_env = backs[i].had_var = 0;
            continue;
        }
        const char *oe = getenv(backs[i].name);
        backs[i].had_env = oe != NULL;
        backs[i].env = oe ? strdup(oe) : NULL;
        if (oe && !backs[i].env) {
            free(backs[i].name);
            backs[i].name = NULL;
            continue;
        }
        const char *ov = get_shell_var(backs[i].name);
        backs[i].had_var = ov != NULL;
        backs[i].var = ov ? strdup(ov) : NULL;
        if (ov && !backs[i].var) {
            free(backs[i].env);
            free(backs[i].name);
            backs[i].name = NULL;
            continue;
        }
        char *val = eq + 1;
        size_t vlen = strlen(val);
        if (vlen > 1 && val[0] == '(' && val[vlen-1] == ')') {
            char *body = strndup(val+1, vlen-2);
            if (!body) {
                free(backs[i].env);
                free(backs[i].var);
                free(backs[i].name);
                backs[i].name = NULL;
                continue;
            }
            char *p = body;
            char **vals = NULL; int count=0; int failed = 0;
            size_t joinlen = 0;
            while (*p) {
                while (*p==' '||*p=='\t') p++;
                if (*p=='\0') break;
                char *start=p;
                while (*p && *p!=' ' && *p!='\t') p++;
                if (*p) *p++='\0';
                char **tmp = realloc(vals, sizeof(char*)*(count+1));
                if (!tmp) {
                    failed = 1;
                    break;
                }
                vals = tmp;
                vals[count] = strdup(start);
                if (!vals[count]) {
                    failed = 1;
                    break;
                }
                count++;
                joinlen += strlen(start) + 1;
            }
            if (!failed) {
                char *joined = malloc(joinlen+1);
                if (joined) {
                    joined[0] = '\0';
                    for (int j=0;j<count;j++) {
                        strcat(joined, vals[j]);
                        if (j < count-1) strcat(joined, " ");
                    }
                }
                if (backs[i].name) {
                    setenv(backs[i].name, joined ? joined : "", 1);
                    set_shell_array(backs[i].name, vals, count);
                }
                free(joined);
            }
            for(int j=0;j<count;j++) free(vals[j]);
            free(vals);
            free(body);
        } else {
            if (backs[i].name) {
                setenv(backs[i].name, val, 1);
                set_shell_var(backs[i].name, val);
            }
        }
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

    if (seg->close_out)
        close(seg->out_fd);
    else if (seg->dup_out != -1)
        dup2(seg->dup_out, seg->out_fd);
    if (seg->close_err)
        close(STDERR_FILENO);
    else if (seg->dup_err != -1)
        dup2(seg->dup_err, STDERR_FILENO);
}


/*
 * Fork and execute each segment of a pipeline, wiring up pipes between
 * processes.  wait_for_pipeline() is used to collect child statuses when
 * running in the foreground.  Returns the value assigned to last_status.
 */
static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line) {
    int seg_count = 0;
    for (PipelineSegment *tmp = pipeline; tmp; tmp = tmp->next)
        seg_count++;
    pid_t *pids = calloc(seg_count, sizeof(pid_t));
    if (!pids)
        return 1;

    int spawned = 0;
    int in_fd = -1;
    for (PipelineSegment *seg = pipeline; seg; seg = seg->next) {
        pid_t pid = fork_segment(seg, &in_fd);
        if (pid < 0) {
            if (in_fd != -1)
                close(in_fd);
            /* Wait for already spawned children */
            if (spawned > 0)
                wait_for_pipeline(pids, spawned, 0, line);
            free(pids);
            last_status = 1;
            return 1;
        }
        pids[spawned++] = pid;
    }

    if (in_fd != -1)
        close(in_fd);

    wait_for_pipeline(pids, spawned, background, line);
    free(pids);
    return last_status;
}

static int run_pipeline_internal(PipelineSegment *pipeline, int background, const char *line) {
    if (!pipeline)
        return 0;

    if (opt_xtrace && line) {
        const char *ps4 = getenv("PS4");
        if (!ps4) ps4 = "+ ";
        fprintf(stderr, "%s%s\n", ps4, line);
    }

    if (apply_temp_assignments(pipeline)) {
        cleanup_proc_subs();
        return last_status;
    }

    if (!pipeline->argv[0] || pipeline->argv[0][0] == '\0') {
        fprintf(stderr, "syntax error: missing command\n");
        last_status = 1;
        cleanup_proc_subs();
        return last_status;
    }
    int r = spawn_pipeline_segments(pipeline, background, line);
    cleanup_proc_subs();
    return r;
}

int run_command_list(Command *cmds, const char *line) {
    CmdOp prev = OP_SEMI;
    for (Command *c = cmds; c; c = c->next) {
        int run = 1;
        if (c != cmds) {
            if (prev == OP_AND)
                run = (last_status == 0);
            else if (prev == OP_OR)
                run = (last_status != 0);
        }
        if (run) {
            if (opt_noexec) {
                if (c->type == CMD_PIPELINE && c->pipeline &&
                    c->pipeline->argv[0] &&
                    strcmp(c->pipeline->argv[0], "set") == 0)
                    run_pipeline(c, line);
            } else {
                run_pipeline(c, line);
            }
        }
        prev = c->op;
        if (func_return || loop_break || loop_continue)
            break;
    }
    if (loop_depth == 0) {
        loop_break = 0;
        loop_continue = 0;
    }
    return last_status;
}

/*
 * Execute a simple or composed pipeline.  This wraps
 * run_pipeline_internal() and returns its resulting status.
 */
static int exec_pipeline(Command *cmd, const char *line) {
    return run_pipeline_internal(cmd->pipeline, cmd->background, line);
}

/*
 * Define a shell function.  The body of the command becomes the new
 * function definition and last_status is preserved.
 */
static int exec_funcdef(Command *cmd, const char *line) {
    (void)line;
    define_function(cmd->var, cmd->body, cmd->text);
    cmd->body = NULL;
    return last_status;
}

/*
 * Execute an if/else construct.  The condition list is run first and
 * the body or else part is selected based on its exit status.  Returns
 * the status of the last command executed.
 */
static int exec_if(Command *cmd, const char *line) {
    run_command_list(cmd->cond, line);
    if (last_status == 0)
        run_command_list(cmd->body, line);
    else if (cmd->else_part)
        run_command_list(cmd->else_part, line);
    return last_status;
}

/*
 * Execute a while loop.  The condition list is repeatedly evaluated
 * and the body executed while it returns success.  Loop control flags
 * loop_break and loop_continue are honored.  Returns last_status.
 */
static int exec_while(Command *cmd, const char *line) {
    loop_depth++;
    while (1) {
        run_command_list(cmd->cond, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) { loop_continue--; continue; }
        if (last_status != 0)
            break;
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) { loop_continue--; continue; }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute an until loop.  This is the inverse of a while loop: the
 * body runs repeatedly until the condition list succeeds.  Loop
 * control flags are processed and the final last_status is returned.
 */
static int exec_until(Command *cmd, const char *line) {
    loop_depth++;
    while (1) {
        run_command_list(cmd->cond, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) { loop_continue--; continue; }
        if (last_status == 0)
            break;
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) { loop_continue--; continue; }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute a for loop over a list of words.  The variable specified by
 * cmd->var is set to each word in turn before the body is executed.
 * Returns last_status from the final iteration.
 */
static int exec_for(Command *cmd, const char *line) {
    loop_depth++;
    for (int i = 0; i < cmd->word_count; i++) {
        if (cmd->var)
            setenv(cmd->var, cmd->words[i], 1);
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) { loop_continue--; continue; }
    }
    loop_depth--;
    return last_status;
}

/*
 * Implement the POSIX select loop.  A numbered menu is printed for the
 * supplied words and user input chooses which value is assigned to the
 * iteration variable.  The function returns the status of the body.
 */
static int exec_select(Command *cmd, const char *line) {
    loop_depth++;
    char input[MAX_LINE];
    while (1) {
        for (int i = 0; i < cmd->word_count; i++)
            printf("%d) %s\n", i + 1, cmd->words[i]);
        const char *ps3 = getenv("PS3");
        fputs(ps3 ? ps3 : "? ", stdout);
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin))
            break;
        int choice = atoi(input);
        if (choice < 1 || choice > cmd->word_count) {
            continue;
        }
        if (cmd->var)
            setenv(cmd->var, cmd->words[choice - 1], 1);
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) { loop_continue--; continue; }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute a C-style for loop using arithmetic expressions.  The init,
 * condition and update parts are evaluated with eval_arith().  The loop
 * terminates when the condition evaluates to zero.  Returns last_status.
 */
static int exec_for_arith(Command *cmd, const char *line) {
    (void)line;
    loop_depth++;
    eval_arith(cmd->arith_init ? cmd->arith_init : "0");
    while (1) {
        long cond = eval_arith(cmd->arith_cond ? cmd->arith_cond : "1");
        if (cond == 0)
            break;
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        eval_arith(cmd->arith_update ? cmd->arith_update : "0");
        if (loop_continue) { loop_continue--; continue; }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute a case statement.  Each pattern list is matched against the
 * supplied word using fnmatch().  The body of the first matching case
 * is executed and last_status from that body is returned.
 */
static int exec_case(Command *cmd, const char *line) {
    for (CaseItem *ci = cmd->cases; ci; ci = ci->next) {
        int matched = 0;
        for (int i = 0; i < ci->pattern_count; i++) {
            if (fnmatch(ci->patterns[i], cmd->var, 0) == 0) {
                matched = 1;
                break;
            }
        }
        if (matched) {
            run_command_list(ci->body, line);
            if (!ci->fall_through)
                break;
        }
    }
    return last_status;
}

/*
 * Spawn a subshell running the provided command group.  The parent waits
 * for the child to finish and propagates its exit status.  Returns that
 * status or 1 on fork failure.
 */
static int exec_subshell(Command *cmd, const char *line) {
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        run_command_list(cmd->group, line);
        exit(last_status);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            last_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            last_status = 128 + WTERMSIG(status);
        return last_status;
    } else {
        perror("fork");
        last_status = 1;
        return 1;
    }
}

/*
 * Execute the [[ ... ]] conditional command.  This simply forwards to
 * builtin_cond() and returns its result.
 */
static int exec_cond(Command *cmd, const char *line) {
    (void)line;
    builtin_cond(cmd->words);
    return last_status;
}

/*
 * Execute a { ... } command group in the current shell.  Simply runs
 * the contained command list and returns its status.
 */
static int exec_group(Command *cmd, const char *line) {
    run_command_list(cmd->group, line);
    return last_status;
}

int run_pipeline(Command *cmd, const char *line) {
    if (!cmd)
        return 0;
    if (opt_hashall && cmd->type == CMD_PIPELINE) {
        for (PipelineSegment *seg = cmd->pipeline; seg; seg = seg->next) {
            if (seg->argv[0] && !strchr(seg->argv[0], '/'))
                hash_add(seg->argv[0]);
        }
    }
    int r = 0;
    switch (cmd->type) {
    case CMD_PIPELINE:
        r = exec_pipeline(cmd, line); break;
    case CMD_FUNCDEF:
        r = exec_funcdef(cmd, line); break;
    case CMD_IF:
        r = exec_if(cmd, line); break;
    case CMD_WHILE:
        r = exec_while(cmd, line); break;
    case CMD_UNTIL:
        r = exec_until(cmd, line); break;
    case CMD_FOR:
        r = exec_for(cmd, line); break;
    case CMD_SELECT:
        r = exec_select(cmd, line); break;
    case CMD_FOR_ARITH:
        r = exec_for_arith(cmd, line); break;
    case CMD_CASE:
        r = exec_case(cmd, line); break;
    case CMD_SUBSHELL:
        r = exec_subshell(cmd, line); break;
    case CMD_COND:
        r = exec_cond(cmd, line); break;
    case CMD_GROUP:
        r = exec_group(cmd, line); break;
    default:
        r = 0; break;
    }
    if (cmd->negate) {
        last_status = (r == 0 ? 1 : 0);
        r = last_status;
    }
    return r;
}

