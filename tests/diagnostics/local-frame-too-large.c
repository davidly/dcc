/* A frame past the code generator's limit is reported with its size
 * (directive seed 1038463). */
int main(void)
{
    int n = 1;
    int a[32767];

    return a[n - 1];
}
