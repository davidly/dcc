/*
 * Array rank exceeding the supported maximum must be rejected.  C99/C11
 * 5.2.4.1 "Translation limits" requires an implementation to support at least
 * 12 pointer, array, and function declarators in any combination; dcc caps
 * array rank at 12, so a 13-dimension array is a diagnostic, not a silent
 * truncation.
 */
int f(void)
{
    int a[2][2][2][2][2][2][2][2][2][2][2][2][2];
    return 0;
}
