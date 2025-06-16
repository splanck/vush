/*
 * Shell function support
 *
 * Function definitions are kept in a linked list of FuncEntry
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
#include "util.h"

extern int last_status;

static FuncEntry *functions = NULL;

FuncEntry *find_function(const char *name)
{
    for (FuncEntry *f = functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0)
            return f;
    }
    return NULL;
}

/*
 * Determine the file used to persist shell functions.  If the
 * VUSH_FUNCFILE environment variable is set its value is returned,
 * otherwise ~/.vush_funcs is used.
 */
static char *funcfile_path(void)
{
    return make_user_path("VUSH_FUNCFILE", NULL, ".vush_funcs");
}

/*
 * Write the current list of functions to the persistence file.  Each
 * function is saved on a single line in the same form it was defined so
 * it can later be parsed by load_functions().
 */
static void save_functions(void)
{
    char *path = funcfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    free(path);
    if (!f)
        return;
    for (FuncEntry *fn = functions; fn; fn = fn->next)
        fprintf(f, "%s() { %s }\n", fn->name, fn->text);
    fclose(f);
}

/*
 * Read previously saved functions from the persistence file and rebuild
 * the in-memory list by parsing each line.
 */
void load_functions(void)
{
    char *path = funcfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "r");
    free(path);
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
                define_function(c->var, NULL, c->text);
                free_commands(c->body);
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
    for (FuncEntry *f = functions; f; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            char *new_name = strdup(name);
            char *new_text = strdup(text);
            if (!new_name || !new_text) {
                perror("strdup");
                free(new_name);
                free(new_text);
                free_commands(body);
                return;
            }
            free(f->name);
            free(f->text);
            free_commands(f->body);
            f->name = new_name;
            f->text = new_text;
            f->body = NULL;
            free_commands(body);
            return;
        }
    }
    FuncEntry *fn = malloc(sizeof(FuncEntry));
    if (!fn) {
        perror("malloc");
        free_commands(body);
        return;
    }
    char *name_copy = strdup(name);
    char *text_copy = strdup(text);
    if (!name_copy || !text_copy) {
        perror("strdup");
        free(name_copy);
        free(text_copy);
        free(fn);
        free_commands(body);
        return;
    }
    fn->name = name_copy;
    fn->text = text_copy;
    fn->body = NULL;
    free_commands(body);
    fn->next = functions;
    functions = fn;
}

/* Look up the parsed body of a function by name. */
Command *get_function(const char *name)
{
    FuncEntry *f = find_function(name);
    return f ? f->body : NULL;
}

void print_functions(void)
{
    for (FuncEntry *fn = functions; fn; fn = fn->next)
        printf("%s() { %s }\n", fn->name, fn->text);
}

/* Remove NAME from the function list if present. */
void remove_function(const char *name)
{
    FuncEntry *prev = NULL;
    for (FuncEntry *f = functions; f; prev = f, f = f->next) {
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
    FuncEntry *f = functions;
    while (f) {
        FuncEntry *next = f->next;
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

