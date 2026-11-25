/* A block-scope object needs a complete type (directive seed 1014233). */
struct Missing;

void f(void)
{
    struct Missing m;
}
