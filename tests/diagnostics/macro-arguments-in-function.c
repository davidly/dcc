/* A wrong macro call in a function body is reported, not compiled as 0. */
#define F(a, b) ((a) + (b))
#define G(a) (a)

int f(void)
{
    return F(1);
}

int g(void)
{
    return G(1, 2);
}
