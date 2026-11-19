/* A file-scope name declared as both an object and a function. */
unsigned short count = 5;

static long count(void)
{
    return 1;
}

int main(void)
{
    return 0;
}
