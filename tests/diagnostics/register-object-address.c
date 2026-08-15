int main(void)
{
    register int value;
    int *pointer = &value;

    return pointer != 0;
}