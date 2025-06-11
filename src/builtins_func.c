/*
 * Shell function support
 *
 * Function definitions are kept in a linked list of struct func_entry
 * records. Each entry stores the function name, the parsed command body
 * and the original source text so it can be written back verbatim.
 *
 * On shell exit free_functions() serializes this list one definition per
 * line and writes it to the file returned by funcfile_path().  The path
 * defaults to ~/.vush_funcs but can be overridden with the VUSH_FUNCFILE
 * environment variable.  load_functions() reads the same file at start up
 * and recreates the in-memory list by parsing each saved line.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "parser.h" /* for MAX_LINE */
#include "func_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

extern int last_status;

struct func_entry {
    char *name;
    char *text;
    Command *body;
    struct func_entry *next;
};

static struct func_entry *functions = NULL;

/*
 * Determine the file used to persist shell functions.  If the
 * VUSH_FUNCFILE environment variable is set its value is returned,
 * otherwise ~/.vush_funcs is used.
 */
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

/*
 * Write the current list of functions to the persistence file.  Each
 * function is saved on a single line in the same form it was defined so
 * it can later be parsed by load_functions().
 */
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

/*
 * Read previously saved functions from the persistence file and rebuild
 * the in-memory list by parsing each line.
 */
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

/*
 * Add or replace a function definition.  The original text of the
 * function body is kept so save_functions() can write it back verbatim.
 */
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

/* Look up the parsed body of a function by name. */
Command *get_function(const char *name)
{
    for (struct func_entry *f = functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0)
            return f->body;
    }
    return NULL;
}

/* Remove NAME from the function list if present. */
void remove_function(const char *name)
{
    struct func_entry *prev = NULL;
    for (struct func_entry *f = functions; f; prev = f, f = f->next) {
        if (strcmp(f->name, name) == 0) {
            if (prev)
                prev->next = f->next;
            else
                functions = f->next;
            free(f->name);
            free(f->text);
            free_commands(f->body);
            free(f);
            return;
        }
    }
}

/* Free all function entries without saving them. */
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

/* Save functions and free memory at shell shutdown. */
void free_functions(void)
{
    save_functions();
    free_function_entries();
}

/*
 * Implementation of the 'return' builtin for shell functions.  Sets the
 * function's exit status and signals the executor to unwind to the caller.
 */
int builtin_return(char **args)
{
    int status = 0;
    if (args[1])
        status = atoi(args[1]);
    last_status = status;
    func_return = 1;
    return 1;
}

