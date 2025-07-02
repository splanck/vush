/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Loading and saving the history file.
 */

#define _GNU_SOURCE
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "util.h"
#include "state_paths.h"
#include "error.h"
#include "shell_state.h"

/* functions from history_list.c */
void history_add_entry(const char *cmd, int save_file);
void history_list_iter(void (*cb)(const char *cmd, void *arg), void *arg);
void history_renumber(void);


/*
 * Append ``cmd`` as a single line to the on-disk history file.  This is used
 * whenever a new command is entered so that the file mirrors the in-memory
 * list.
 */
void history_file_append(const char *cmd) {
    char *path = get_history_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine history file location\n");
        return;
    }
    FILE *f = fopen(path, "a");
    if (!f) {
        fprintf(stderr, "warning: unable to open history file for appending\n");
        last_status = 1;
        free(path);
        return;
    }
    free(path);
    fprintf(f, "%s\n", cmd);
    fclose(f);
}

/* Context used when rewriting the entire history file.  ``error`` is set to 1
 * when a write fails so that the caller can detect incomplete output. */
struct rewrite_ctx {
    FILE *f;
    int error;
};

/* Callback passed to ``history_list_iter`` that writes each command to ``ctx``. */
static void rewrite_cb(const char *cmd, void *arg) {
    struct rewrite_ctx *ctx = arg;
    if (fprintf(ctx->f, "%s\n", cmd) < 0)
        ctx->error = 1;
}

/*
 * Rewrite the history file so that it contains exactly the commands stored in
 * memory.  This is called after deletions or when the in-memory limit differs
 * from the file limit.
 */
void history_file_rewrite(void) {
    char *path = get_history_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine history file location\n");
        return;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "warning: unable to open history file for writing\n");
        last_status = 1;
        free(path);
        return;
    }
    struct rewrite_ctx ctx = { .f = f, .error = 0 };
    history_list_iter(rewrite_cb, &ctx);
    if (fclose(f) == EOF)
        ctx.error = 1;
    if (ctx.error) {
        fprintf(stderr, "warning: failed to write history file\n");
        last_status = 1;
        unlink(path);
    }
    free(path);
}

/* Remove all contents from the history file. */
void history_file_clear(void) {
    char *path = get_history_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine history file location\n");
        return;
    }
    FILE *f = fopen(path, "w");
    free(path);
    if (f)
        fclose(f);
}

/*
 * Read the on-disk history file and populate the in-memory list.  Each line
 * becomes a ``HistEntry``.  After loading the IDs are renumbered and the file
 * rewritten to enforce size limits.
 */
void load_history(void) {
    char *path = get_history_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine history file location\n");
        return;
    }
    FILE *f = fopen(path, "r");
    free(path);
    if (!f)
        return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        history_add_entry(line, 0);
    }
    fclose(f);
    history_renumber();
    history_file_rewrite();
}
