#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct HistEntry {
    int id;
    char cmd[MAX_LINE];
    struct HistEntry *next;
    struct HistEntry *prev;
} HistEntry;

static HistEntry *head = NULL;
static HistEntry *tail = NULL;
static HistEntry *cursor = NULL;
static int next_id = 1;
static int skip_next = 0;
static int history_size = 0;
static int max_history = MAX_HISTORY;

static void history_init(void) {
    static int inited = 0;
    if (inited)
        return;
    const char *env = getenv("VUSH_HISTSIZE");
    if (env) {
        long val = strtol(env, NULL, 10);
        if (val > 0)
            max_history = (int)val;
    }
    inited = 1;
}

static void add_history_entry(const char *cmd, int save_file) {
    history_init();
    HistEntry *e = malloc(sizeof(HistEntry));
    if (!e) return;
    e->id = next_id++;
    strncpy(e->cmd, cmd, MAX_LINE - 1);
    e->cmd[MAX_LINE - 1] = '\0';
    e->next = NULL;
    if (!head) {
        e->prev = NULL;
        head = tail = e;
    } else {
        tail->next = e;
        e->prev = tail;
        tail = e;
    }
    history_size++;

    if (history_size > max_history) {
        HistEntry *old = head;
        head = head->next;
        if (head)
            head->prev = NULL;
        else
            tail = NULL;
        free(old);
        history_size--;
    }

    if (save_file) {
        const char *home = getenv("HOME");
        if (home) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/.vush_history", home);
            FILE *f = fopen(path, "a");
            if (f) {
                fprintf(f, "%s\n", cmd);
                fclose(f);
            }
        }
    }
}

void add_history(const char *cmd) {
    if (skip_next) {
        skip_next = 0;
        return;
    }
    add_history_entry(cmd, 1);
}

void print_history(void) {
    for (HistEntry *e = head; e; e = e->next) {
        printf("%d %s\n", e->id, e->cmd);
    }
}

void load_history(void) {
    history_init();
    const char *home = getenv("HOME");
    if (!home)
        return;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.vush_history", home);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        add_history_entry(line, 0);
    }
    fclose(f);
}

const char *history_prev(void) {
    if (!tail)
        return NULL;
    if (!cursor)
        cursor = tail;
    else if (cursor->prev)
        cursor = cursor->prev;
    return cursor ? cursor->cmd : NULL;
}

const char *history_next(void) {
    if (!cursor)
        return NULL;
    if (cursor->next)
        cursor = cursor->next;
    else
        cursor = NULL;
    return cursor ? cursor->cmd : NULL;
}

void history_reset_cursor(void) {
    cursor = NULL;
}

void clear_history(void) {
    HistEntry *e = head;
    while (e) {
        HistEntry *next = e->next;
        free(e);
        e = next;
    }
    head = tail = cursor = NULL;
    next_id = 1;
    skip_next = 1;
    history_size = 0;

    const char *home = getenv("HOME");
    if (home) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/.vush_history", home);
        FILE *f = fopen(path, "w");
        if (f)
            fclose(f);
    }
}

