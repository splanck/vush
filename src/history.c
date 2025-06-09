#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

typedef struct HistEntry {
    int id;
    char cmd[MAX_LINE];
    struct HistEntry *next;
    struct HistEntry *prev;
} HistEntry;

static HistEntry *head = NULL;
static HistEntry *tail = NULL;
static HistEntry *cursor = NULL;
static HistEntry *search_cursor = NULL;
static int next_id = 1;
static int skip_next = 0;
static int history_size = 0;
static int max_history = MAX_HISTORY;

static const char *histfile_path(void) {
    const char *env = getenv("VUSH_HISTFILE");
    if (env && *env)
        return env;
    const char *home = getenv("HOME");
    if (!home)
        return NULL;
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.vush_history", home);
    return path;
}

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
        const char *path = histfile_path();
        if (path) {
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
    const char *path = histfile_path();
    if (!path)
        return;
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

const char *history_search_prev(const char *term) {
    if (!term || !*term || !tail)
        return NULL;
    HistEntry *start = search_cursor ? search_cursor->prev : tail;
    for (HistEntry *e = start; e; e = e->prev) {
        if (strstr(e->cmd, term)) {
            search_cursor = e;
            return e->cmd;
        }
    }
    return NULL;
}

const char *history_search_next(const char *term) {
    if (!term || !*term || !head)
        return NULL;
    HistEntry *start = search_cursor ? search_cursor->next : head;
    for (HistEntry *e = start; e; e = e->next) {
        if (strstr(e->cmd, term)) {
            search_cursor = e;
            return e->cmd;
        }
    }
    return NULL;
}

void history_reset_search(void) {
    search_cursor = NULL;
}

void clear_history(void) {
    HistEntry *e = head;
    while (e) {
        HistEntry *next = e->next;
        free(e);
        e = next;
    }
    head = tail = cursor = search_cursor = NULL;
    next_id = 1;
    history_size = 0;

    const char *path = histfile_path();
    if (path) {
        FILE *f = fopen(path, "w");
        if (f)
            fclose(f);
    }
}

void delete_history_entry(int id) {
    history_init();
    HistEntry *e = head;
    while (e && e->id != id)
        e = e->next;
    if (!e)
        return;

    if (e->prev)
        e->prev->next = e->next;
    else
        head = e->next;

    if (e->next)
        e->next->prev = e->prev;
    else
        tail = e->prev;

    if (cursor == e)
        cursor = e->next;
    if (search_cursor == e)
        search_cursor = e->next;

    free(e);
    history_size--;

    const char *path = histfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (HistEntry *h = head; h; h = h->next)
        fprintf(f, "%s\n", h->cmd);
    fclose(f);
}

const char *history_last(void) {
    return tail ? tail->cmd : NULL;
}

const char *history_find_prefix(const char *prefix) {
    if (!prefix || !*prefix)
        return NULL;
    size_t len = strlen(prefix);
    for (HistEntry *e = tail; e; e = e->prev) {
        if (strncmp(e->cmd, prefix, len) == 0)
            return e->cmd;
    }
    return NULL;
}

