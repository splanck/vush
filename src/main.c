/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 * Main entry point and REPL loop.
 *
 * Command line arguments are parsed to either execute a single command
 * with `-c` or to run a script file.  Additional arguments become
 * `script_argv` so scripts can access them.
 *
 * After initialization the shell enters a read‑eval‑print loop that reads
 * lines from the chosen input, performs history expansion and parsing,
 * then executes the resulting pipelines while tracking their exit status.
 */


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include "common.h"

#include "parser.h"
#include "jobs.h"
#include "builtins.h"
#include "execute.h"
#include "lexer.h"
#include "history.h"
#include "lineedit.h"
#include "scriptargs.h"
#include "options.h"
#include "dirstack.h"
#include "util.h"
#include "version.h"
#include "hash.h"

extern FILE *parse_input;
extern char *trap_cmds[NSIG];
extern char *exit_trap_cmd;
void trap_handler(int sig);
void run_exit_trap(void);
static volatile sig_atomic_t pending_traps[NSIG];
int last_status = 0;
int param_error = 0;
int script_argc = 0;
char **script_argv = NULL;
int opt_errexit = 0;
int opt_nounset = 0;
int opt_xtrace = 0;
int opt_verbose = 0;
int opt_pipefail = 0;
int opt_noclobber = 0;
int opt_noexec = 0;
int opt_noglob = 0;
int opt_allexport = 0;
int opt_monitor = 1;
int opt_notify = 1;
int opt_privileged = 0;
int opt_onecmd = 0;
int opt_hashall = 0;
int opt_keyword = 0;
int current_lineno = 0;
pid_t parent_pid = 0;

static int process_rc_file(const char *path, FILE *input);
static int process_startup_file(FILE *input);
static void run_command_string(const char *cmd);
static void check_mail(void);
static void repl_loop(FILE *input);
static int process_pending_traps(void);
static int any_pending_traps(void);

/*
 * Record that a trapped signal was received. Execution of the
 * associated trap command is deferred until it is safe to run
 * normal shell code from the main loop.
 */
void trap_handler(int sig)
{
    if (sig > 0 && sig < NSIG)
        pending_traps[sig] = 1;
}

/* Execute any queued trap commands. Returns the number executed. */
static int process_pending_traps(void)
{
    int ran = 0;
    for (int s = 1; s < NSIG; s++) {
        if (pending_traps[s]) {
            pending_traps[s] = 0;
            char *cmd = trap_cmds[s];
            if (!cmd)
                continue;
            FILE *prev = parse_input;
            parse_input = stdin;
            Command *cmds = parse_line(cmd);
            if (cmds) {
                CmdOp prevop = OP_SEMI;
                for (Command *c = cmds; c; c = c->next) {
                    int run = 1;
                    if (c != cmds) {
                        if (prevop == OP_AND)
                            run = (last_status == 0);
                        else if (prevop == OP_OR)
                            run = (last_status != 0);
                    }
                    if (run)
                        run_pipeline(c, cmd);
                    prevop = c->op;
                }
            }
            free_commands(cmds);
            parse_input = prev;
            ran = 1;
        }
    }
    return ran;
}

/* Check if any traps are waiting to be executed. */
static int any_pending_traps(void)
{
    for (int s = 1; s < NSIG; s++)
        if (pending_traps[s])
            return 1;
    return 0;
}

/* Execute the command registered for EXIT, if any. */
void run_exit_trap(void)
{
    if (!exit_trap_cmd)
        return;
    FILE *prev = parse_input;
    parse_input = stdin;
    Command *cmds = parse_line(exit_trap_cmd);
    if (cmds) {
        CmdOp prevop = OP_SEMI;
        for (Command *c = cmds; c; c = c->next) {
            int run = 1;
            if (c != cmds) {
                if (prevop == OP_AND)
                    run = (last_status == 0);
                else if (prevop == OP_OR)
                    run = (last_status != 0);
            }
            if (run)
                run_pipeline(c, exit_trap_cmd);
            prevop = c->op;
        }
    }
    free_commands(cmds);
    parse_input = prev;
    free(exit_trap_cmd);
    exit_trap_cmd = NULL;
}

/*
 * Source the user's ~/.vushrc file if present. Each line is expanded,
 * parsed and executed before starting the main loop. The commands are
 * added to history and `parse_input` is temporarily set to the rc file.
 */
static int process_rc_file(const char *path, FILE *input)
{
    FILE *rc = fopen(path, "r");
    if (!rc)
        return 0;

    int executed = 0;

    char rcline[MAX_LINE];
    while (read_logical_line(rc, rcline, sizeof(rcline))) {
        current_lineno++;
        if (opt_verbose)
            printf("%s\n", rcline);
        char *exp = expand_history(rcline);
        if (!exp)
            continue;
        parse_input = rc;
        Command *cmds = parse_line(exp);
        if (!cmds) {
            free_commands(cmds);
            if (exp != rcline)
                free(exp);
            continue;
        }

        add_history(rcline);

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
                run_pipeline(c, exp);
            prev = c->op;
        }
        free_commands(cmds);
        if (exp != rcline)
            free(exp);
        executed = 1;
    }
    fclose(rc);
    parse_input = input;
    return executed;
}

static int process_startup_file(FILE *input)
{
    const char *home = getenv("HOME");
    if (!home)
        return 0;
    char rcpath[PATH_MAX];
    snprintf(rcpath, sizeof(rcpath), "%s/.vushrc", home);
    return process_rc_file(rcpath, input);
}

/*
 * Execute a command string supplied via the -c option. The string
 * undergoes history expansion, is parsed into pipelines and then
 * executed as if typed interactively. The command is added to history.
 */
static void run_command_string(const char *cmd)
{
    char linebuf[MAX_LINE];
    strncpy(linebuf, cmd, sizeof(linebuf) - 1);
    linebuf[sizeof(linebuf) - 1] = '\0';
    char *line = linebuf;

    if (opt_verbose)
        printf("%s\n", line);

    char *expanded = expand_history(line);
    if (!expanded)
        return;

    parse_input = stdin;
    Command *cmds = parse_line(expanded);
    if (cmds) {
        add_history(line);

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
                run_pipeline(c, expanded);
            prev = c->op;
        }
    }
    free_commands(cmds);
    if (expanded != line)
        free(expanded);
}

/*
 * Check mailbox files specified by $MAIL or $MAILPATH and print a
 * notification when new mail arrives. The last modification time of each
 * path is remembered so messages are only shown when the file is updated
 * between prompts.
 */
typedef struct MailEntry {
    char *path;
    time_t mtime;
    struct MailEntry *next;
} MailEntry;

static MailEntry *mail_list = NULL;

static MailEntry *find_mail_entry(const char *path)
{
    for (MailEntry *e = mail_list; e; e = e->next)
        if (strcmp(e->path, path) == 0)
            return e;
    return NULL;
}

static void remember_mail_time(const char *path, time_t mtime)
{
    MailEntry *e = find_mail_entry(path);
    if (e) {
        e->mtime = mtime;
        return;
    }
    e = malloc(sizeof(*e));
    if (!e)
        return;
    e->path = strdup(path);
    if (!e->path) {
        free(e);
        return;
    }
    e->mtime = mtime;
    e->next = mail_list;
    mail_list = e;
}

/* Free all MailEntry structures tracked for mail notifications. */
static void free_mail_list(void)
{
    MailEntry *e = mail_list;
    while (e) {
        MailEntry *next = e->next;
        free(e->path);
        free(e);
        e = next;
    }
    mail_list = NULL;
}

static void check_mail(void)
{
    char *mpath = getenv("MAILPATH");
    char *mail = getenv("MAIL");
    char *list[64];
    int count = 0;

    if (mpath && *mpath) {
        char *dup = strdup(mpath);
        if (!dup)
            return;
        char *tok = strtok(dup, ":");
        while (tok && count < 64) {
            list[count++] = tok;
            tok = strtok(NULL, ":");
        }
        for (int i = 0; i < count; i++) {
            struct stat st;
            if (stat(list[i], &st) == 0) {
                MailEntry *e = find_mail_entry(list[i]);
                if (e && st.st_mtime > e->mtime)
                    printf("New mail in %s\n", list[i]);
                remember_mail_time(list[i], st.st_mtime);
            }
        }
        free(dup);
        return;
    }

    if (mail && *mail) {
        struct stat st;
        if (stat(mail, &st) == 0) {
            MailEntry *e = find_mail_entry(mail);
            if (e && st.st_mtime > e->mtime)
                printf("You have mail.\n");
            remember_mail_time(mail, st.st_mtime);
        }
    }
}

/*
 * Primary read‑eval‑print loop. Reads a line from the given input (stdin
 * or a script), expands history, parses and executes it. In interactive
 * mode a prompt is displayed and background jobs are checked each
 * iteration. Loop terminates on EOF or line_edit returning NULL.
 */
static void repl_loop(FILE *input)
{
    char linebuf[MAX_LINE];
    char *line;
    int interactive = (input == stdin);

    while (1) {
        process_pending_traps();
        if (opt_monitor)
            check_jobs();
        else
            while (waitpid(-1, NULL, WNOHANG) > 0)
                ;
        if (interactive) {
            check_mail();
            const char *ps = getenv("PS1");
            char *prompt = expand_prompt(ps ? ps : "vush> ");
            jobs_at_prompt = 1;
            check_jobs();
            if (jobs_at_prompt)
                line = line_edit(prompt);
            else
                line = line_edit("");
            jobs_at_prompt = 0;
            free(prompt);
            if (!line) {
                if (any_pending_traps()) {
                    if (interactive)
                        printf("\n");
                    process_pending_traps();
                    continue;
                }
                break;
            }
            current_lineno++;
        } else {
            if (!read_logical_line(input, linebuf, sizeof(linebuf))) {
                if (process_pending_traps())
                    continue;
                break;
            }
            current_lineno++;
            line = linebuf;
        }

        if (opt_verbose)
            printf("%s\n", line);

        char *cmdline = strdup(line);
        if (line != linebuf)
            free(line);
        if (!cmdline) {
            perror("strdup");
            break;
        }

        while (1) {
            char *expanded = expand_history(cmdline);
            if (!expanded) {
                free(cmdline);
                cmdline = NULL;
                break;
            }

            parse_input = input;
            Command *cmds = parse_line(expanded);
            if (parse_need_more) {
                free_commands(cmds);
                free(expanded);
                const char *ps2 = getenv("PS2");
                char *more = NULL;
                if (interactive) {
                    char *p2 = expand_prompt(ps2 ? ps2 : "> ");
                    jobs_at_prompt = 1;
                    more = line_edit(p2);
                    jobs_at_prompt = 0;
                    free(p2);
                    if (!more) {
                        free(cmdline);
                        cmdline = NULL;
                        if (any_pending_traps()) {
                            printf("\n");
                            process_pending_traps();
                        }
                        break;
                    }
                    current_lineno++;
                } else {
                    if (!read_logical_line(input, linebuf, sizeof(linebuf))) {
                        free(cmdline);
                        cmdline = NULL;
                        if (any_pending_traps())
                            process_pending_traps();
                        break;
                    }
                    current_lineno++;
                    more = strdup(linebuf);
                }
                if (opt_verbose)
                    printf("%s\n", more);
                size_t len1 = strlen(cmdline);
                size_t len2 = strlen(more);
                char *tmp = malloc(len1 + len2 + 2);
                if (!tmp) {
                    perror("malloc");
                    free(cmdline);
                    free(more);
                    cmdline = NULL;
                    break;
                }
                memcpy(tmp, cmdline, len1);
                tmp[len1] = '\n';
                memcpy(tmp + len1 + 1, more, len2 + 1);
                free(cmdline);
                free(more);
                cmdline = tmp;
                continue;
            }

            if (!cmds) {
                if (feof(input))
                    clearerr(input);
                free_commands(cmds);
                free(expanded);
                break;
            }

            add_history(cmdline);

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
                    run_pipeline(c, expanded);
                prev = c->op;
            }
            free_commands(cmds);
            free(expanded);
            process_pending_traps();
            break;
        }

        free(cmdline);
        if (opt_onecmd)
            break;
    }
}

int main(int argc, char **argv) {

    FILE *input = stdin;
    char *dash_c = NULL;

    /* Always expose the running shell as $SHELL */
    setenv("SHELL", argv[0], 1);

    if (!getenv("PWD")) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)))
            setenv("PWD", cwd, 1);
    }

    parent_pid = getppid();

    if (argc > 1) {
        if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
            printf("vush %s\n", VUSH_VERSION);
            return 0;
        } else if (strcmp(argv[1], "-c") == 0) {
            if (argc < 3) {
                fprintf(stderr, "usage: %s -c command\n", argv[0]);
                return 1;
            }
            dash_c = argv[2];
        } else {
            input = fopen(argv[1], "r");
            if (!input) {
                perror(argv[1]);
                return 1;
            }

            script_argc = argc - 2;
            script_argv = calloc(script_argc + 2, sizeof(char *));
            if (!script_argv) {
                perror("calloc");
                return 1;
            }
            script_argv[0] = strdup(argv[1]);
            if (!script_argv[0]) {
                perror("strdup");
                free(script_argv);
                script_argv = NULL;
                script_argc = 0;
                return 1;
            }
            for (int i = 0; i < script_argc; i++) {
                script_argv[i + 1] = strdup(argv[i + 2]);
                if (!script_argv[i + 1]) {
                    perror("strdup");
                    for (int j = 0; j <= i; j++)
                        free(script_argv[j]);
                    free(script_argv);
                    script_argv = NULL;
                    script_argc = 0;
                    return 1;
                }
            }
            script_argv[script_argc + 1] = NULL;
        }
    }

    /* Ignore Ctrl-C in the shell itself */
    signal(SIGINT, SIG_IGN);
    /* Reap background jobs asynchronously */
    signal(SIGCHLD, jobs_sigchld_handler);

    load_history();
    load_aliases();
    load_functions();

    int rc_ran = 0;
    if (!opt_privileged)
        rc_ran = process_startup_file(input);

    const char *envfile = getenv("ENV");
    int env_ran = 0;
    if (envfile && *envfile)
        env_ran = process_rc_file(envfile, input);

    if (input == stdin && (rc_ran || env_ran))
        printf("\n");

    if (dash_c)
        run_command_string(dash_c);
    else
        repl_loop(input);
    if (input != stdin)
        fclose(input);
    run_exit_trap();
    clear_history();
    dirstack_clear();
    if (script_argv) {
        for (int i = 0; i <= script_argc; i++)
            free(script_argv[i]);
        free(script_argv);
        getopts_pos = NULL;
    }
    free_aliases();
    free_mail_list();
    free_functions();
    hash_clear();
    free_trap_cmds();
    return dash_c ? last_status : 0;
}

