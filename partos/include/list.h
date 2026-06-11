#ifndef LIST_H
#define LIST_H

/*
 * Generic singly linked lists.
 *
 * Convention: every list-able structure has its next pointer as the
 * FIRST member. The list head variable and a member's next field are
 * then identical cells — a 16-bit word holding the address of the next
 * member, or 0 at the end — so one set of routines (list.s) handles
 * every list in the system, regardless of the member type:
 *
 *   head ──> member ──> member ──> 0
 *   (cell)   (next is   (next is
 *            the cell)  the cell)
 *
 * A structure participates in a list by starting with list_t (or,
 * equivalently, with its own typed next pointer as the first member —
 * the layout is what matters, not the type):
 *
 *   typedef struct dev_s {
 *       struct dev_s *next;   // first member: list link
 *       ...                   // payload
 *   } dev_t;
 *
 * A chain is one or more members linked through their first word and
 * terminated with 0. A single member is a chain of one: its next word
 * must be 0 before it is appended.
 */
typedef struct list_s {
    struct list_s *next;
} list_t;

/*
 * Appends a chain (or a single member with next == 0) to the end of a
 * list. list is the ADDRESS of the head pointer, so appending to an
 * empty list correctly sets the head:
 *
 *   append_list((list_t **)&dev_list, (list_t *)probe_result);
 */
void append_list(list_t **list, list_t *chain);

#endif /* LIST_H */
