#include <ctype.h>
#include <stdio.h>

#ifdef ACTION_UNSIGNED_TEXT
typedef unsigned char ActionChar;
#define ACTION_LITERAL(text) ((const ActionChar *)(text))
#else
typedef char ActionChar;
#define ACTION_LITERAL(text) (text)
#endif

#ifdef ACTION_VOLATILE_STATE
#define ACTION_STATE volatile
#else
#define ACTION_STATE
#endif

#ifdef ACTION_ALT_CONSTANTS
#define ACT_NONE 10
#define ACT_GOTO 11
#define ACT_RETURN 12
#define ACT_ASSIGN 13
#else
#define ACT_NONE 0
#define ACT_GOTO 1
#define ACT_RETURN 2
#define ACT_ASSIGN 3
#endif

#ifdef ACTION_RENAMED_HELPERS
#define trim action_trim
#define starts action_starts
#define parse_goto_label action_parse_label
#define find_char action_find_char
#define decode_assignment_fields action_decode_assignment
#endif

struct Stmt {
#ifdef ACTION_PAD_LAYOUT
    int padding;
#endif
    int act;
    int act_target_label;
    int assigned;
};

static void trim(ActionChar *s)
{
    int first = 0;
    int last;
    int out;

    while (s[first] && isspace((unsigned char)s[first]))
        ++first;
    out = 0;
    while (s[first])
        s[out++] = s[first++];
    s[out] = 0;
    last = out;
    while (last > 0 && isspace((unsigned char)s[last - 1]))
        s[--last] = 0;
}

static int starts(const ActionChar *s, const ActionChar *prefix)
{
    while (*prefix) {
        if (toupper((unsigned char)*s) != *prefix)
            return 0;
        ++s;
        ++prefix;
    }
    return 1;
}

static int parse_goto_label(ActionChar *s)
{
    int value = 0;

    while (*s && !isdigit((unsigned char)*s))
        ++s;
    while (isdigit((unsigned char)*s))
        value = value * 10 + *s++ - '0';
    return value;
}

static ActionChar *find_char(ActionChar *s, int c)
{
    while (*s) {
        if (*s == c)
            return s;
        ++s;
    }
    return 0;
}

static void decode_assignment_fields(
    ACTION_STATE struct Stmt *st, ActionChar *s, int ifpart)
{
    st->act = ACT_ASSIGN;
    st->assigned = ifpart + (find_char(s, '=') != 0);
}

static void decode_action(
    ACTION_STATE struct Stmt *st, ActionChar *q)
{
    trim(q);
    st->act = ACT_NONE;
    if (starts(q, ACTION_LITERAL("GOTO")) ||
        starts(q, ACTION_LITERAL("GO TO"))) {
        st->act = ACT_GOTO;
        st->act_target_label = parse_goto_label(q);
        return;
    }
    if (starts(q, ACTION_LITERAL("RETURN"))) {
        st->act = ACT_RETURN;
        return;
    }
    if (find_char(q, '=')) {
        decode_assignment_fields(st, q, 1);
        return;
    }
}

static void run_case(const char *text)
{
    struct Stmt statement;
    ActionChar buffer[32];
    int i = 0;

    statement.act = -1;
    statement.act_target_label = -1;
    statement.assigned = 0;
    while (text[i]) {
        buffer[i] = (ActionChar)text[i];
        ++i;
    }
    buffer[i] = 0;
    decode_action(&statement, buffer);
    printf("%d,%d,%d\n", statement.act,
           statement.act_target_label, statement.assigned);
}

int main(void)
{
    run_case(" GOTO 123 ");
    run_case("GO TO 77");
    run_case("RETURN");
    run_case("X=5");
    run_case("UNKNOWN");
    return 0;
}
