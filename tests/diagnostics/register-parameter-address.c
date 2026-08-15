static int read(register int value)
{
    return *(&value);
}

int main(void)
{
    return read(1);
}