#define _GNU_SOURCE
#include "builtins.h"
#include "vars.h"
#include "scriptargs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int last_status;

/* Pointer into the current $@ item being parsed by getopts. When script_argv
 * is replaced or freed this must be cleared so it does not reference stale
 * memory. */
char *getopts_pos = NULL;

static int read_optind(void)
{
    const char *ind_s = get_shell_var("OPTIND");
    int ind = ind_s ? atoi(ind_s) : 1;
    if (ind < 1)
        ind = 1;
    if (!script_argv)
        ind = 1;
    return ind;
}

static void write_optind(int ind)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", ind);
    set_shell_var("OPTIND", buf);
}

static void write_optarg(const char *val)
{
    set_shell_var("OPTARG", val ? val : "");
}

static void write_result_var(const char *var, const char *val)
{
    set_shell_var(var, val);
}

enum {
    OPT_OK,
    OPT_DONE,
    OPT_ILLEGAL,
    OPT_MISSING
};

static int getopts_next_option(const char *optstr, int silent, int *ind, char *opt)
{
    if (!script_argv || *ind > script_argc) {
        getopts_pos = NULL;
        return OPT_DONE;
    }

    if (!getopts_pos || *getopts_pos == '\0') {
        char *arg = script_argv[*ind];
        if (strcmp(arg, "--") == 0) {
            (*ind)++;
            getopts_pos = NULL;
            return OPT_DONE;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            getopts_pos = NULL;
            return OPT_DONE;
        }
        getopts_pos = arg + 1;
        (*ind)++;
    }

    *opt = *getopts_pos++;
    if (!*opt || *opt == ':')
        *opt = '?';

    const char *p = strchr(optstr, *opt);
    if (!p) {
        if (!silent)
            fprintf(stderr, "getopts: illegal option -- %c\n", *opt);
        if (silent) {
            char ob[2] = {*opt, '\0'};
            write_optarg(ob);
        } else {
            write_optarg("");
        }
        if (!getopts_pos || *getopts_pos == '\0')
            getopts_pos = NULL;
        return OPT_ILLEGAL;
    }

    if (p[1] == ':') {
        if (*getopts_pos != '\0') {
            write_optarg(getopts_pos);
            getopts_pos = NULL;
        } else if (*ind <= script_argc) {
            write_optarg(script_argv[*ind]);
            (*ind)++;
            getopts_pos = NULL;
        } else {
            if (!silent)
                fprintf(stderr, "getopts: option requires an argument -- %c\n", *opt);
            if (silent) {
                char ob[2] = {*opt, '\0'};
                write_optarg(ob);
            } else {
                write_optarg("");
            }
            return OPT_MISSING;
        }
    } else {
        write_optarg("");
        if (!getopts_pos || *getopts_pos == '\0')
            getopts_pos = NULL;
    }
    return OPT_OK;
}

int builtin_getopts(char **args) {
    if (!args[1] || !args[2]) {
        fprintf(stderr, "usage: getopts optstring var\n");
        last_status = 1;
        return 1;
    }

    const char *optstr = args[1];
    const char *var = args[2];
    int silent = 0;
    if (optstr[0] == ':') {
        silent = 1;
        optstr++;
    }

    int ind = read_optind();

    char opt = '\0';
    int res = getopts_next_option(optstr, silent, &ind, &opt);

    switch (res) {
    case OPT_OK: {
        char val[2] = {opt, '\0'};
        write_result_var(var, val);
        last_status = 0;
        break;
    }
    case OPT_DONE:
        write_result_var(var, "?");
        write_optarg("");
        last_status = 1;
        break;
    case OPT_ILLEGAL:
        write_result_var(var, "?");
        last_status = 0;
        break;
    case OPT_MISSING:
        if (silent)
            write_result_var(var, ":");
        else
            write_result_var(var, "?");
        last_status = 0;
        break;
    }

    write_optind(ind);
    return 1;
}

