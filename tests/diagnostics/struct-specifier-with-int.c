/* A struct specifier cannot be combined with int. */
struct C { int a; };

void p(int struct C *r)
{
    r->a = 1;
}
