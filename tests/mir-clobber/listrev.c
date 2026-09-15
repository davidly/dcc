#include <stdio.h>

#ifdef LISTREV_RENAMED
#define REVERSE_FUNCTION reverse_fixture_renamed
#else
#define REVERSE_FUNCTION reverse_fixture
#endif

#ifdef LISTREV_VOLATILE_NEXT
#define NEXT_QUALIFIER volatile
#else
#define NEXT_QUALIFIER
#endif

struct ListNode {
    int value;
    struct ListNode * NEXT_QUALIFIER next;
};

static struct ListNode *REVERSE_FUNCTION(struct ListNode *head)
{
#ifdef LISTREV_EXTRA_CFG
    if (head == NULL)
        return head;
#endif
#ifdef LISTREV_RENAMED_LOCALS
    struct ListNode *prior_node;
    struct ListNode *current_node;

    prior_node = NULL;
    current_node = head;
    while (current_node != NULL) {
        struct ListNode *following_node = current_node->next;
        current_node->next = prior_node;
        prior_node = current_node;
        current_node = following_node;
    }
    return prior_node;
#else
    struct ListNode *previous;
    struct ListNode *current;

    previous = NULL;
    current = head;
    while (current != NULL) {
        struct ListNode *next = current->next;
        current->next = previous;
        previous = current;
        current = next;
    }
    return previous;
#endif
}

int main(void)
{
    static const int expected_values[6] = {
        -30000, 1234, 0, 32767, -1, 17
    };
    struct ListNode nodes[6];
    struct ListNode *head;
    struct ListNode *cursor;
    int failures = 0;
    int index;
    long checksum = 0;

    for (index = 0; index < 6; ++index) {
        nodes[index].value = expected_values[5 - index];
        nodes[index].next = index == 5 ? NULL : &nodes[index + 1];
    }
    head = REVERSE_FUNCTION(&nodes[0]);
    cursor = head;
    for (index = 0; index < 6; ++index) {
        if (cursor == NULL) {
            ++failures;
            break;
        }
        if (cursor != &nodes[5 - index])
            ++failures;
        if (cursor->value != expected_values[index])
            ++failures;
        checksum = checksum * 7L + cursor->value;
        cursor = cursor->next;
    }
    if (cursor != NULL)
        ++failures;
    if (checksum != -499641573L)
        ++failures;
    printf("list reverse failures=%d checksum=%ld head=%d tail=%d\n",
           failures, checksum, head->value, nodes[0].value);
    return failures != 0;
}
