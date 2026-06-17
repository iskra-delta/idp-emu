/*
 * Declares the intrusive singly linked-list helpers shared across kernel
 * objects and system-owned resources.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef LIST_H
#define LIST_H

#include <stddef.h>
#include <stdint.h>

/*
 * Intrusive singly linked-list node header shared across kernel objects.
 * Any list-able structure starts with this next pointer at offset 0.
 */
typedef struct list_item_s {
    struct list_item_s *next;
    uint8_t data[0];
} list_item_t;

/* compatibility alias used by older PartOS code */
typedef list_item_t list_t;

/*
 * Match helper that compares one element argument for equality.
 */
uint8_t list_match_eq(list_item_t *p, uint16_t arg);

/*
 * Find the first element for which the callback returns true.
 */
list_item_t *list_find(
    list_item_t *first,
    list_item_t **prev,
    uint8_t (*match)(list_item_t *p, uint16_t arg),
    uint16_t the_arg);

/*
 * Visit each element in the list with a caller callback.
 */
void list_iterate(
    list_item_t *first,
    void (*fn)(list_item_t *p, uint16_t arg),
    uint16_t the_arg);

/*
 * Insert one element at the list head.
 */
list_item_t *list_insert(list_item_t **first, list_item_t *el);

/*
 * Remove one explicit element from the list.
 */
list_item_t *list_remove(list_item_t **first, list_item_t *el);

/*
 * Append one element or a pre-linked chain to the list tail.
 * The return value is the appended head, or NULL on failure.
 */
list_item_t *list_append(list_item_t **list, list_item_t *chain);

#endif /* LIST_H */
