#include <ctype.h>
#include <stdio.h>

static int fails;
static int evals;

static void check(int ok)
{
    if (!ok)
        fails++;
}

static int graph_arg(void)
{
    evals++;
    return '!';
}

int main(void)
{
    check(!isalpha(0x0141));
    check(!isalnum(0x0137));
    check(!isspace(0x0120));
    check(!isdigit(0x0139));
    check(!isupper(0x0151));
    check(!islower(0x0171));
    check(!isxdigit(0x0166));
    check(!isprint(0x0121));
    check(!isgraph(0x0121));
    check(!iscntrl(0x0100));
    check(!ispunct(0x0121));
    check(toupper(0x016d) == 0x016d);
    check(tolower(0x014d) == 0x014d);
    check(toupper('m') == 'M');
    check(tolower('M') == 'm');
    check(toupper(-1) == -1);
    check(isgraph(graph_arg()) != 0);
    check(evals == 1);

    if (fails) {
        printf("tctypw FAILED %d\n", fails);
        return 1;
    }
    puts("tctypw ok");
    return 0;
}
