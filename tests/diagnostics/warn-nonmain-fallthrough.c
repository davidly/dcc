/* Converse of warn-main-fallthrough-exempt.c: a non-main, non-void function
 * that falls off the end MUST warn. This pins the mechanism the main exemption
 * suppresses, so the exemption test cannot pass merely because the warning was
 * deleted outright. Expected: clean exit (warnings are non-fatal) with the
 * warning text in the baseline. */
int helper(int a)
{
    int b;
    b = a + 1;
}

int main(void)
{
    return helper(2);
}
