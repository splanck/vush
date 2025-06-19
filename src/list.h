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

static inline void list_init(List *list) {
    list->head = NULL;
    list->tail = NULL;
}

static inline void list_append(List *list, ListNode *node) {
    node->next = NULL;
    node->prev = list->tail;
    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;
    list->tail = node;
}

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

#define LIST_ENTRY(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#endif /* VUSH_LIST_H */
