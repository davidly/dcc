/* A store whose value names a field the struct lacks is reported, rather
 * than reaching code generation (syntax-crash seed 1177552). */
struct S { int a; char t[4]; } *G;

void f(int i)
{
    if (i)
        G->t[i] = (char)G->c;
}
