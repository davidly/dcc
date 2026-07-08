#include <stdio.h>

struct G {
    char a;
    char b;
    char c;
};

struct G garr[4];
struct G g1;
struct G *garrp = &garr[3];
char *gbp = &g1.b;
void *nullbp = &((struct G *)0)->b;

int main(void)
{
    printf("garr offset %d\n", (int)((char *)garrp - (char *)garr));
    printf("field offset %d\n", (int)(gbp - (char *)&g1));
    printf("null field offset %d\n", (int)((char *)nullbp - (char *)0));
    return 0;
}
