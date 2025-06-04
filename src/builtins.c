/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#define _GNU_SOURCE
#include "builtins.h"
#include "jobs.h"
#include "history.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

static int builtin_cd(char **args) {
    const char *dir = args[1] ? args[1] : getenv("HOME");
    if (chdir(dir) != 0) {
        perror("cd");
    }
    return 1;
}

static int builtin_exit(char **args) {
    (void)args;
    exit(0);
}

static int builtin_pwd(char **args) {
    (void)args;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
    return 1;
}

static int builtin_jobs(char **args) {
    (void)args;
    print_jobs();
    return 1;
}

static int builtin_history(char **args) {
    (void)args;
    print_history();
    return 1;
}

static int builtin_export(char **args) {
    if (!args[1] || !strchr(args[1], '=')) {
        fprintf(stderr, "usage: export NAME=value\n");
        return 1;
    }

    char *pair = args[1];
    char *eq = strchr(pair, '=');
    *eq = '\0';
    if (setenv(pair, eq + 1, 1) != 0) {
        perror("export");
    }
    *eq = '=';
    return 1;
}

static int builtin_source(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: source file\n");
        return 1;
    }

    FILE *input = fopen(args[1], "r");
    if (!input) {
        perror(args[1]);
        return 1;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), input)) {
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
    fclose(input);
    return 1;
}

static int builtin_help(char **args) {
    (void)args;
    printf("Built-in commands:\n");
    printf("  cd [dir]   Change the current directory\n");
    printf("  exit       Exit the shell\n");
    printf("  pwd        Print the current working directory\n");
    printf("  jobs       List running background jobs\n");
    printf("  export NAME=value   Set an environment variable\n");
    printf("  history    Show command history\n");
    printf("  source FILE (. FILE)   Execute commands from FILE\n");
    printf("  help       Display this help message\n");
    return 1;
}

struct builtin {
    const char *name;
    int (*func)(char **);
};

static struct builtin builtins[] = {
    {"cd", builtin_cd},
    {"exit", builtin_exit},
    {"pwd", builtin_pwd},
    {"jobs", builtin_jobs},
    {"export", builtin_export},
    {"history", builtin_history},
    {"source", builtin_source},
    {".", builtin_source},
    {"help", builtin_help},
    {NULL, NULL}
};

int run_builtin(char **args) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(args[0], builtins[i].name) == 0) {
            return builtins[i].func(args);
        }
    }
    return 0;
}

