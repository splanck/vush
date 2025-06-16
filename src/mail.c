#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mail.h"

struct MailEntry {
    char *path;
    time_t mtime;
    struct MailEntry *next;
};

static struct MailEntry *mail_list = NULL;

struct MailEntry *find_mail_entry(const char *path)
{
    for (struct MailEntry *e = mail_list; e; e = e->next)
        if (strcmp(e->path, path) == 0)
            return e;
    return NULL;
}

void remember_mail_time(const char *path, time_t mtime)
{
    struct MailEntry *e = find_mail_entry(path);
    if (e) {
        e->mtime = mtime;
        return;
    }
    e = malloc(sizeof(*e));
    if (!e)
        return;
    e->path = strdup(path);
    if (!e->path) {
        free(e);
        return;
    }
    e->mtime = mtime;
    e->next = mail_list;
    mail_list = e;
}

void free_mail_list(void)
{
    struct MailEntry *e = mail_list;
    while (e) {
        struct MailEntry *next = e->next;
        free(e->path);
        free(e);
        e = next;
    }
    mail_list = NULL;
}

void check_mail(void)
{
    char *mpath = getenv("MAILPATH");
    char *mail = getenv("MAIL");
    char *list[64];
    int count = 0;

    if (mpath && *mpath) {
        char *dup = strdup(mpath);
        if (!dup)
            return;
        char *tok = strtok(dup, ":");
        while (tok && count < 64) {
            list[count++] = tok;
            tok = strtok(NULL, ":");
        }
        for (int i = 0; i < count; i++) {
            struct stat st;
            if (stat(list[i], &st) == 0) {
                struct MailEntry *e = find_mail_entry(list[i]);
                if (e && st.st_mtime > e->mtime)
                    printf("New mail in %s\n", list[i]);
                remember_mail_time(list[i], st.st_mtime);
            }
        }
        free(dup);
        return;
    }

    if (mail && *mail) {
        struct stat st;
        if (stat(mail, &st) == 0) {
            struct MailEntry *e = find_mail_entry(mail);
            if (e && st.st_mtime > e->mtime)
                printf("You have mail.\n");
            remember_mail_time(mail, st.st_mtime);
        }
    }
}

