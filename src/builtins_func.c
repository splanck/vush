/* Shell function support */
#define _GNU_SOURCE
#include "builtins.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <linux/limits.h>

extern int last_status;
extern int func_return;

struct func_entry {
    char *name;
    char *text;
    Command *body;
    struct func_entry *next;
};

static struct func_entry *functions = NULL;

static const char *funcfile_path(void)
{
    const char *env = getenv("VUSH_FUNCFILE");
    if (env && *env)
        return env;
    const char *home = getenv("HOME");
    if (!home)
        return NULL;
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.vush_funcs", home);
    return path;
}

static void save_functions(void)
{
    const char *path = funcfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (struct func_entry *fn = functions; fn; fn = fn->next)
        fprintf(f, "%s() { %s }\n", fn->name, fn->text);
    fclose(f);
}

void load_functions(void)
{
    const char *path = funcfile_path();
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
        Command *cmds = parse_line(line);
        for (Command *c = cmds; c; c = c->next) {
            if (c->type == CMD_FUNCDEF) {
                define_function(c->var, c->body, c->text);
                c->body = NULL;
            }
        }
        free_commands(cmds);
    }
    fclose(f);
}

void define_function(const char *name, Command *body, const char *text)
{
    for (struct func_entry *f = functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            free(f->name);
            free(f->text);
            free_commands(f->body);
            f->name = strdup(name);
            f->text = strdup(text);
            f->body = body;
            return;
        }
    }
    struct func_entry *fn = malloc(sizeof(struct func_entry));
    if (!fn) {
        perror("malloc");
        return;
    }
    fn->name = strdup(name);
    fn->text = strdup(text);
    fn->body = body;
    fn->next = functions;
    functions = fn;
}

Command *get_function(const char *name)
{
    for (struct func_entry *f = functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0)
            return f->body;
    }
    return NULL;
}

static void free_function_entries(void)
{
    struct func_entry *f = functions;
    while (f) {
        struct func_entry *next = f->next;
        free(f->name);
        free(f->text);
        free_commands(f->body);
        free(f);
        f = next;
    }
    functions = NULL;
}

void free_functions(void)
{
    save_functions();
    free_function_entries();
}

int builtin_return(char **args)
{
    int status = 0;
    if (args[1])
        status = atoi(args[1]);
    last_status = status;
    func_return = 1;
    return 1;
}

