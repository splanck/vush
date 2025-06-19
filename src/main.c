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
#include "shell_state.h"
#include "scriptargs.h"
#include "dirstack.h"
#include "util.h"
#include "version.h"
#include "hash.h"
#include "trap.h"
#include "startup.h"
#include "mail.h"
#include "repl.h"


ShellState shell_state = {
    .opt_monitor = 1,
    .opt_notify = 1
};

#include "options.h"

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
            script_argv = xcalloc(script_argc + 2, sizeof(char *));
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

