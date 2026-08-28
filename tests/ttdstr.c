#include <stdio.h>

typedef char Row[5];
typedef Row Pair[2];
typedef Pair Cube[2];

static const Cube values = {
    {"1", "12"},
    {"123", "1234"}
};
static const Pair *first = &values[0];
static const Pair *second = &values[1];
static const Pair *pointers[2] = {&values[0], &values[1]};

static int first_char(const char *text)
{
    return text[0];
}

static int pointer_table_checks(void)
{
    if (first_char(*(*(pointers[0]) + 1)) != '1' ||
        first_char(*(*(pointers[1]) + 0)) != '1' ||
        first_char(*(*(pointers[1]) - 1)) != '1')
        return 1;
    if (first_char(*(*(pointers[0]) + 0) + 1) != 0 ||
        first_char(*(*(pointers[0]) + 1) + 1) != '2' ||
        first_char(*(*(pointers[1]) + 0) + 2) != '3')
        return 2;
    return 0;
}

int main(void)
{
    char first_value;
    char second_value;

    if (sizeof(Row) != 5 || sizeof(Pair) != 10 || sizeof(Cube) != 20)
        return 1;
    if (values[0][0][0] != '1' || values[0][0][1] != 0)
        return 2;
    if (values[0][1][1] != '2' || values[0][1][2] != 0)
        return 3;
    if (values[1][0][2] != '3' || values[1][0][3] != 0)
        return 4;
    if (values[1][1][3] != '4' || values[1][1][4] != 0)
        return 5;
    if (first != &values[0] || second != &values[1])
        return 6;
    first_value = (*first)[1][1];
    second_value = (*second)[0][2];
    if (first_value != '2' || second_value != '3')
        return 7;
    if (first_char(*((*first) + 1)) != '1' ||
        first_char(*((*second) + 0)) != '1')
        return 8;
    if (first_char(*(first[0] + 1)) != '1' ||
        first_char(*(second[0] + 0)) != '1')
        return 9;
    if (first_char((*first)[0] + 1) != 0 ||
        first_char((*first)[1] + 1) != '2' ||
        first_char((*second)[0] + 2) != '3')
        return 10;
    if (pointer_table_checks())
        return 11;
    printf("ttdstr completed with great success\n");
    return 0;
}
