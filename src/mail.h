/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Mail checking helpers.
 */

#ifndef MAIL_H
#define MAIL_H
#include <time.h>
/* Free internal list tracking mail timestamps. */
void free_mail_list(void);
/* Examine configured mailboxes and print a message when new mail arrives. */
void check_mail(void);
/* Helper functions exported for completeness. */
struct MailEntry;
/* Locate entry for PATH in the internal list or NULL if none. */
struct MailEntry *find_mail_entry(const char *path);
/* Record modification time for PATH. */
void remember_mail_time(const char *path, time_t mtime);
#endif /* MAIL_H */
