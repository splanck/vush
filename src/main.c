#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_TOKENS 64
#define MAX_LINE 1024

typedef struct Job {
    int id;
    pid_t pid;
    char cmd[MAX_LINE];
    struct Job *next;
} Job;

static Job *jobs = NULL;
static int next_job_id = 1;

static void add_job(pid_t pid, const char *cmd) {
    Job *job = malloc(sizeof(Job));
    if (!job) return;
    job->id = next_job_id++;
    job->pid = pid;
    strncpy(job->cmd, cmd, MAX_LINE - 1);
    job->cmd[MAX_LINE - 1] = '\0';
    job->next = jobs;
    jobs = job;
}

static void remove_job(pid_t pid) {
    Job **curr = &jobs;
    while (*curr) {
        if ((*curr)->pid == pid) {
            Job *tmp = *curr;
            *curr = (*curr)->next;
            free(tmp);
            return;
        }
        curr = &((*curr)->next);
    }
}

static void check_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[vush] job %d finished\n", pid);
        remove_job(pid);
    }
}

static void print_jobs(void) {
    Job *j = jobs;
    while (j) {
        printf("[%d] %d %s\n", j->id, j->pid, j->cmd);
        j = j->next;
    }
}

static char *expand_var(const char *token) {
    if (token[0] != '$') return strdup(token);
    const char *val = getenv(token + 1);
    return strdup(val ? val : "");
}

static int parse_line(char *line, char **args, int *background) {
    int argc = 0;
    *background = 0;
    char *p = line;
    while (*p && argc < MAX_TOKENS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        char *start = p;
        if (*p == '"') {
            start = ++p;
            while (*p && *p != '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        int len = p - start;
        /* clamp length to avoid overflowing the temporary buffer */
        if (len >= MAX_LINE)
            len = MAX_LINE - 1;
        char buf[MAX_LINE];
        strncpy(buf, start, len);
        buf[len] = '\0';
        args[argc++] = expand_var(buf);
        if (*p) p++;
    }
    if (argc > 0 && strcmp(args[argc-1], "&") == 0) {
        *background = 1;
        free(args[argc-1]);
        argc--;
    }
    args[argc] = NULL;
    return argc;
}

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

static struct builtin {
    const char *name;
    int (*func)(char **);
} builtins[] = {
    {"cd", builtin_cd},
    {"exit", builtin_exit},
    {"pwd", builtin_pwd},
    {"jobs", builtin_jobs},
    {NULL, NULL}
};

static int run_builtin(char **args) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(args[0], builtins[i].name) == 0) {
            return builtins[i].func(args);
        }
    }
    return 0;
}

int main(void) {
    char line[MAX_LINE];
    char *args[MAX_TOKENS];

    while (1) {
        check_jobs();
        printf("vush> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t len = strlen(line);
        if (len && line[len-1] == '\n') line[len-1] = '\0';
        int background = 0;
        int argc = parse_line(line, args, &background);
        if (argc == 0) continue;
        if (run_builtin(args)) {
            for (int i = 0; i < argc; i++) free(args[i]);
            continue;
        }
        pid_t pid = fork();
        if (pid == 0) {
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
        for (int i = 0; i < argc; i++) free(args[i]);
    }
    return 0;
}

