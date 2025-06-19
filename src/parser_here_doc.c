#define _GNU_SOURCE
#include "parser_here_doc.h"
#include "lexer.h"
#include "var_expand.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int process_here_doc(PipelineSegment *seg, char **p, char *tok, int quoted) {
    if (quoted || strncmp(tok, "<<", 2) != 0)
        return 0;
    if ((tok[2] == '<') || (**p == '<'))
        return 0;
    int strip_tabs = 0;
    char *rest = tok + 2;
    if (*rest == '-') {
        strip_tabs = 1;
        rest++;
    }
    char *delim;
    int delim_quoted = 0;
    if (*rest) {
        char *rp = rest;
        int de = 1;
        delim = read_token(&rp, &delim_quoted, &de);
        if (!delim || *rp) {
            free(delim);
            delim = strdup(rest);
            delim_quoted = 0;
        }
        if (!delim) {
            free(tok);
            return -1;
        }
        size_t dlen = strlen(delim);
        if (!delim_quoted && dlen >= 2 &&
            ((delim[0] == '"' && delim[dlen - 1] == '"') ||
             (delim[0] == '\'' && delim[dlen - 1] == '\''))) {
            delim_quoted = 1;
            memmove(delim, delim + 1, dlen - 2);
            delim[dlen - 2] = '\0';
        }
    } else {
        while (**p == ' ' || **p == '\t') (*p)++;
        int de = 1;
        delim = read_token(p, &delim_quoted, &de);
        if (!delim) { free(tok); return -1; }
        size_t dlen = strlen(delim);
        if (!delim_quoted && dlen >= 2 &&
            ((delim[0] == '"' && delim[dlen - 1] == '"') ||
             (delim[0] == '\'' && delim[dlen - 1] == '\''))) {
            delim_quoted = 1;
            memmove(delim, delim + 1, dlen - 2);
            delim[dlen - 2] = '\0';
        }
    }
    char template[] = "/tmp/vushXXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) { perror("mkstemp"); free(delim); free(tok); return -1; }
    FILE *tf = fdopen(fd, "w");
    if (!tf) { perror("fdopen"); close(fd); unlink(template); free(delim); free(tok); return -1; }
    FILE *in = parse_input ? parse_input : stdin;
    char buf[MAX_LINE];
    size_t pos = 0;
    int found = 0;
    int c;
    int got_eof = 0;
    while ((c = fgetc(in)) != EOF) {
        if (c == 4 && isatty(fileno(in))) {
            got_eof = 1;
            c = EOF;
            break;
        }
        if (c == '\r') {
            if (!isatty(fileno(in))) {
                int n = fgetc(in);
                if (n != '\n' && n != EOF)
                    ungetc(n, in);
            }
            c = '\n';
        }
        if (c == '\n') {
            buf[pos] = '\0';
            char *line = buf;
            if (strip_tabs) {
                while (*line == '\t') line++;
            }
            if (strcmp(line, delim) == 0) { found = 1; break; }
            char *out_line = line;
            if (!delim_quoted) {
                out_line = expand_var(line);
                if (!out_line) {
                    fclose(tf);
                    unlink(template);
                    free(delim);
                    free(tok);
                    return -1;
                }
            }
            fprintf(tf, "%s\n", out_line);
            if (!delim_quoted)
                free(out_line);
            pos = 0;
        } else if (pos < sizeof(buf) - 1) {
            buf[pos++] = c;
        }
    }
    if (c == EOF && pos > 0 && !found) {
        buf[pos] = '\0';
        char *line = buf;
        if (strip_tabs) {
            while (*line == '\t') line++;
        }
        if (strcmp(line, delim) == 0) {
            found = 1;
        } else {
            char *out_line = line;
            if (!delim_quoted) {
                out_line = expand_var(line);
                if (!out_line) {
                    fclose(tf);
                    unlink(template);
                    free(delim);
                    free(tok);
                    return -1;
                }
            }
            if (fprintf(tf, "%s\n", out_line) < 0) {
                if (!delim_quoted)
                    free(out_line);
                fclose(tf);
                unlink(template);
                free(delim);
                free(tok);
                return -1;
            }
            if (!delim_quoted)
                free(out_line);
        }
    }
    if (!found) {
        int eof = feof(in) || got_eof;
        if (in == stdin && feof(in))
            clearerr(stdin);
        fclose(tf);
        unlink(template);
        free(delim);
        free(tok);
        if (eof) {
            fprintf(stderr, "syntax error: here-document delimited by end-of-file\n");
            fflush(stderr);
        }
        else
            parse_need_more = 1;
        return -1;
    }
    fclose(tf);
    seg->in_file = strdup(template);
    if (!seg->in_file) {
        unlink(template);
        free(delim);
        free(tok);
        return -1;
    }
    seg->here_doc = 1;
    seg->here_doc_quoted = delim_quoted;
    free(delim);
    free(tok);
    return 1;
}

int parse_here_string(PipelineSegment *seg, char **p, char *tok) {
    if (!((strcmp(tok, "<<") == 0 && **p == '<') || strncmp(tok, "<<<", 3) == 0))
        return 0;
    if (strncmp(tok, "<<<", 3) == 0 && tok[3] == '<') {
        free(tok);
        return -1;
    }
    if (strcmp(tok, "<<") == 0 && **p == '<')
        (*p)++;
    while (**p == ' ' || **p == '\t') (*p)++;
    char *word = NULL;
    if (strncmp(tok, "<<<", 3) == 0 && tok[3]) {
        word = strdup(tok + 3);
        if (!word) { free(tok); return -1; }
    } else if (**p) {
        int q = 0; int de = 1;
        word = read_token(p, &q, &de);
        if (!word) { free(tok); return -1; }
    } else {
        word = strdup("");
        if (!word) { free(tok); return -1; }
    }
    char template[] = "/tmp/vushXXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) { perror("mkstemp"); free(word); free(tok); return -1; }
    FILE *tf = fdopen(fd, "w");
    if (!tf) { perror("fdopen"); close(fd); unlink(template); free(word); free(tok); return -1; }
    fprintf(tf, "%s\n", word);
    fclose(tf);
    seg->in_file = strdup(template);
    if (!seg->in_file) {
        unlink(template);
        free(word);
        free(tok);
        return -1;
    }
    seg->here_doc = 1;
    free(word);
    free(tok);
    return 1;
}
