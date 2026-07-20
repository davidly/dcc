/* tptrinit (was c8906): regression - a block-scope pointer initialized at its
 * declaration from a struct pointer member (e.g. `struct Cell *next =
 * cur->next;` inside a loop body) must keep its pointer type; dcc previously
 * mistyped it as int and rejected the later `cur = next;` with DCC-E0920.
 * Scenario: singly linked list built on the heap, then reversed in place. */
#include <stdio.h>
#include <stdlib.h>

struct Cell {
    int value;
    struct Cell *next;
};

static struct Cell *list_prepend(struct Cell *head, int value)
{
    struct Cell *node;
    node = (struct Cell *)malloc(sizeof *node);
    if (node == NULL) return head;
    node->value = value;
    node->next = head;
    return node;
}

static struct Cell *list_reverse(struct Cell *head)
{
    struct Cell *prev;
    struct Cell *cur;

    prev = NULL;
    cur = head;
    while (cur != NULL) {
        struct Cell *next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
    return prev;
}

static long list_fold(const struct Cell *head, int *count)
{
    long sum = 0;
    *count = 0;
    while (head != NULL) {
        sum = sum + head->value;
        *count = *count + 1;
        head = head->next;
    }
    return sum;
}

static void list_free(struct Cell *head)
{
    while (head != NULL) {
        struct Cell *next = head->next;
        free(head);
        head = next;
    }
}

static int array_pointer_offsets(void)
{
    char buffer[32];
    char *left;
    char *right;

    left = buffer + (3 * 4);
    right = 4 + buffer;
    return (left - buffer) + (right - buffer);
}

int main(void)
{
    struct Cell *head;
    int i;
    int count;
    long sum;
    int first_before;
    int first_after;

    head = NULL;
    for (i = 1; i <= 10; i = i + 1) head = list_prepend(head, i * i);
    first_before = head->value;
    head = list_reverse(head);
    first_after = head->value;
    sum = list_fold(head, &count);
    printf("c8906 count=%d sum=%ld first_before=%d first_after=%d offsets=%d\n",
           count, sum, first_before, first_after, array_pointer_offsets());
    list_free(head);
    return 0;
}
