#define _GNU_SOURCE
#include "vars.h"
#include "options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct var_entry {
    char *name;
    char *value;        /* scalar value or NULL when array is used */
    char **array;       /* NULL for scalar variables */
    int array_len;
    struct var_entry *next;
};

static struct var_entry *shell_vars = NULL;

struct readonly_entry {
    char *name;
    struct readonly_entry *next;
};

static struct readonly_entry *readonly_vars = NULL;

static int is_readonly(const char *name)
{
    for (struct readonly_entry *r = readonly_vars; r; r = r->next) {
        if (strcmp(r->name, name) == 0)
            return 1;
    }
    return 0;
}

void add_readonly(const char *name)
{
    if (is_readonly(name))
        return;
    struct readonly_entry *r = malloc(sizeof(*r));
    if (!r)
        return;
    r->name = strdup(name);
    if (!r->name) {
        free(r);
        return;
    }
    r->next = readonly_vars;
    readonly_vars = r;
}

void print_readonly_vars(void)
{
    for (struct readonly_entry *r = readonly_vars; r; r = r->next) {
        const char *val = get_shell_var(r->name);
        if (val)
            printf("readonly %s=%s\n", r->name, val);
        else {
            int len = 0;
            char **arr = get_shell_array(r->name, &len);
            if (arr) {
                printf("readonly %s=(", r->name);
                for (int i = 0; i < len; i++) {
                    if (i)
                        printf(" ");
                    printf("%s", arr[i]);
                }
                printf(")\n");
            } else {
                printf("readonly %s\n", r->name);
            }
        }
    }
}

void print_shell_vars(void)
{
    for (struct var_entry *v = shell_vars; v; v = v->next) {
        if (v->array) {
            printf("%s=(", v->name);
            for (int i = 0; i < v->array_len; i++) {
                if (i)
                    printf(" ");
                printf("%s", v->array[i]);
            }
            printf(")\n");
        } else if (v->value) {
            printf("%s=%s\n", v->name, v->value);
        } else {
            printf("%s=\n", v->name);
        }
    }
}

struct local_var {
    char *name;
    char *value;
    char **array;
    int array_len;
    int had_shell;
    int had_env;
    char *env_val;
    struct local_var *next;
};

struct local_frame {
    struct local_var *vars;
    struct local_frame *next;
};

static struct local_frame *local_stack = NULL;

static struct local_var *find_local_var(struct local_frame *f, const char *name) {
    for (struct local_var *v = f ? f->vars : NULL; v; v = v->next) {
        if (strcmp(v->name, name) == 0)
            return v;
    }
    return NULL;
}

int push_local_scope(void) {
    struct local_frame *f = calloc(1, sizeof(*f));
    if (!f)
        return 0;
    f->next = local_stack;
    local_stack = f;
    return 1;
}

void pop_local_scope(void) {
    if (!local_stack)
        return;
    struct local_frame *f = local_stack;
    local_stack = f->next;
    while (f->vars) {
        struct local_var *v = f->vars;
        f->vars = v->next;
        if (v->had_shell) {
            if (v->array)
                set_shell_array(v->name, v->array, v->array_len);
            else
                set_shell_var(v->name, v->value ? v->value : "");
        } else {
            unset_shell_var(v->name);
        }
        if (v->had_env)
            setenv(v->name, v->env_val ? v->env_val : "", 1);
        else
            unsetenv(v->name);
        free(v->name);
        free(v->value);
        if (v->array) {
            for (int i = 0; i < v->array_len; i++)
                free(v->array[i]);
            free(v->array);
        }
        free(v->env_val);
        free(v);
    }
    free(f);
}

void record_local_var(const char *name) {
    if (!local_stack)
        return;
    if (find_local_var(local_stack, name))
        return;
    struct local_var *lv = calloc(1, sizeof(*lv));
    if (!lv)
        return;
    lv->name = strdup(name);
    if (!lv->name) {
        perror("strdup");
        free(lv);
        return;
    }
    const char *val = get_shell_var(name);
    int len = 0;
    char **arr = get_shell_array(name, &len);
    if (arr) {
        lv->array = calloc(len, sizeof(char *));
        if (lv->array) {
            for (int i = 0; i < len; i++)
                lv->array[i] = strdup(arr[i]);
            lv->array_len = len;
        } else {
            fprintf(stderr, "calloc failed in record_local_var\n");
            lv->array_len = 0;
        }
        lv->had_shell = 1;
    } else if (val) {
        lv->value = strdup(val);
        lv->had_shell = 1;
    } else {
        lv->had_shell = 0;
    }
    const char *e = getenv(name);
    if (e) {
        lv->env_val = strdup(e);
        if (lv->env_val) {
            lv->had_env = 1;
        } else {
            /* if strdup fails, pretend there was no env value so we unset */
            lv->had_env = 0;
        }
    } else {
        lv->had_env = 0;
    }
    lv->next = local_stack->vars;
    local_stack->vars = lv;
}

const char *get_shell_var(const char *name) {
    for (struct var_entry *v = shell_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            if (v->value)
                return v->value;
            if (v->array && v->array_len > 0)
                return v->array[0];
            return NULL;
        }
    }
    return NULL;
}

char **get_shell_array(const char *name, int *len) {
    for (struct var_entry *v = shell_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0 && v->array) {
            if (len) *len = v->array_len;
            return v->array;
        }
    }
    if (len) *len = 0;
    return NULL;
}

void set_shell_var(const char *name, const char *value) {
    if (is_readonly(name)) {
        fprintf(stderr, "%s: readonly variable\n", name);
        return;
    }
    for (struct var_entry *v = shell_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            if (v->array) {
                for (int i = 0; i < v->array_len; i++)
                    free(v->array[i]);
                free(v->array);
                v->array = NULL;
                v->array_len = 0;
            }
            char *dup = strdup(value);
            if (!dup) {
                perror("strdup");
                return;
            }
            free(v->value);
            v->value = dup;
            if (opt_allexport)
                setenv(name, v->value ? v->value : "", 1);
            return;
        }
    }
    struct var_entry *v = malloc(sizeof(struct var_entry));
    if (!v) { perror("malloc"); return; }
    v->name = strdup(name);
    if (!v->name) {
        perror("strdup");
        free(v);
        return;
    }
    v->value = strdup(value);
    if (!v->value) {
        perror("strdup");
        free(v->name);
        free(v);
        return;
    }
    v->array = NULL;
    v->array_len = 0;
    v->next = shell_vars;
    shell_vars = v;
    if (opt_allexport)
        setenv(name, value ? value : "", 1);
}

void set_shell_array(const char *name, char **values, int count) {
    if (is_readonly(name)) {
        fprintf(stderr, "%s: readonly variable\n", name);
        return;
    }
    for (struct var_entry *v = shell_vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            size_t alloc_count = count ? count : 1;
            char **new_arr = calloc(alloc_count, sizeof(char *));
            if (!new_arr) {
                perror("calloc");
                return;
            }
            for (int i = 0; i < count; i++) {
                new_arr[i] = strdup(values[i]);
                if (!new_arr[i]) {
                    perror("strdup");
                    for (int j = 0; j < i; j++)
                        free(new_arr[j]);
                    free(new_arr);
                    return;
                }
            }

            if (v->value) {
                free(v->value);
                v->value = NULL;
            }
            if (v->array) {
                for (int i = 0; i < v->array_len; i++)
                    free(v->array[i]);
                free(v->array);
            }
            v->array = new_arr;
            v->array_len = count;
            return;
        }
    }
    struct var_entry *v = calloc(1, sizeof(struct var_entry));
    if (!v) { perror("malloc"); return; }
    v->name = strdup(name);
    if (!v->name) {
        perror("strdup");
        free(v);
        return;
    }
    v->value = NULL;

    size_t alloc_count = count ? count : 1;
    char **new_arr = calloc(alloc_count, sizeof(char *));
    if (!new_arr) {
        perror("calloc");
        free(v->name);
        free(v);
        return;
    }
    for (int i = 0; i < count; i++) {
        new_arr[i] = strdup(values[i]);
        if (!new_arr[i]) {
            perror("strdup");
            for (int j = 0; j < i; j++)
                free(new_arr[j]);
            free(new_arr);
            free(v->name);
            free(v);
            return;
        }
    }

    v->array = new_arr;
    v->array_len = count;
    v->next = shell_vars;
    shell_vars = v;
}

void unset_shell_var(const char *name) {
    if (is_readonly(name)) {
        fprintf(stderr, "%s: readonly variable\n", name);
        return;
    }
    struct var_entry *prev = NULL;
    for (struct var_entry *v = shell_vars; v; prev = v, v = v->next) {
        if (strcmp(v->name, name) == 0) {
            if (prev)
                prev->next = v->next;
            else
                shell_vars = v->next;
            free(v->name);
            free(v->value);
            if (v->array) {
                for (int i = 0; i < v->array_len; i++)
                    free(v->array[i]);
                free(v->array);
            }
            free(v);
            return;
        }
    }
}

void free_shell_vars(void) {
    struct var_entry *v = shell_vars;
    while (v) {
        struct var_entry *n = v->next;
        free(v->name);
        free(v->value);
        if (v->array) {
            for (int i = 0; i < v->array_len; i++)
                free(v->array[i]);
            free(v->array);
        }
        free(v);
        v = n;
    }
    shell_vars = NULL;

    struct readonly_entry *r = readonly_vars;
    while (r) {
        struct readonly_entry *n = r->next;
        free(r->name);
        free(r);
        r = n;
    }
    readonly_vars = NULL;
}

