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
    for (int i0 = 0; i0 < 2; i0++) {
        for (int i1 = 0; i1 < 2; i1++) {
            for (int i2 = 0; i2 < 2; i2++) {
                for (int i3 = 0; i3 < 2; i3++) {
                    for (int i4 = 0; i4 < 2; i4++) {
                        for (int i5 = 0; i5 < 2; i5++) {
                            for (int i6 = 0; i6 < 2; i6++) {
                                for (int i7 = 0; i7 < 2; i7++) {
                                    for (int i8 = 0; i8 < 2; i8++) {
                                        for (int i9 = 0; i9 < 2; i9++) {
                                            for (int i10 = 0; i10 < 2; i10++) {
                                                for (int i11 = 0; i11 < 2; i11++) {
                                                    for (int i12 = 0; i12 < 2; i12++) {
                                                        a[i0][i1][i2][i3][i4][i5][i6][i7][i8][i9][i10][i11][i12] = 1;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}
