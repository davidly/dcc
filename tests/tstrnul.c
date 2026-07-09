/*
 * tstrnul.c - regression test for a real dcc codegen bug: a string literal
 * containing an embedded \0 escape lost every byte after that NUL, because
 * sizeof(), adjacent-literal concatenation, and the emitted data-segment
 * bytes were all computed via strlen()/NUL-scanning instead of the true,
 * lexed byte length (see dcc_symbols.c's read_adjacent_string_literals_ex,
 * dcc_ast_build.c's ast_sizeof_expr_value/AST_STR_LIT handling, and
 * dcc_data.c's string-literal emission).
 */
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* sizeof of a single literal with an embedded NUL: must count every
     * byte plus the terminator, not stop at the first \0. */
    printf("sz1=%d\n", (int)sizeof("A\0B"));
    printf("sz2=%d\n", (int)sizeof("A\0"));

    /* Adjacent-literal concatenation must preserve the first literal's
     * embedded NUL and everything after it (C89 6.1.4): the correct
     * result of "A\0" "B" is the 3-byte sequence 'A','\0','B' (plus the
     * final NUL from "B"), so printf's %s (which stops at the first NUL)
     * must print just "A". */
    printf("adj: %s\n", "A\0" "B");

    /* memcmp confirms the actual emitted bytes are correct, not just that
     * printf happens to stop in the right place. */
    printf("cmp=%d\n", memcmp("A\0" "B", "A\0B", 4));

    return 0;
}
