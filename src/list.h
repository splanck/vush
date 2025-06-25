/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Doubly linked list implementation.
 */

#ifndef VUSH_LIST_H
#define VUSH_LIST_H

#include <stddef.h>

typedef struct ListNode {
    struct ListNode *next;
    struct ListNode *prev;
} ListNode;

typedef struct {
    ListNode *head;
    ListNode *tail;
} List;

/* Initialize an empty doubly linked list. */
static inline void list_init(List *list) {
    list->head = NULL;
    list->tail = NULL;
}

/* Append NODE to the end of LIST. */
static inline void list_append(List *list, ListNode *node) {
    node->next = NULL;
    node->prev = list->tail;
    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;
    list->tail = node;
}

/* Remove NODE from LIST without freeing it. */
static inline void list_remove(List *list, ListNode *node) {
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;
}

#define LIST_FOR_EACH(var, list) \
    for (ListNode *var = (list)->head; var; var = var->next)

/* Obtain the structure containing LIST node PTR. */
#define LIST_ENTRY(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#endif /* VUSH_LIST_H */
