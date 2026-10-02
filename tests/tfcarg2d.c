#include <stdio.h>
#include <string.h>

/* Regression test for a former fastcall argument-decay miscompile
 * (strlen/strchr/memcmp/memset/bdos and
 * the memcpy/memchr/strcpy/strrchr/strstr fastcalls added alongside this
 * test): fastcalls evaluated arguments as values rather than first checking
 * pointer decay, unlike the general argument-evaluation path.
 * A row argument must preserve its address rather than load its first element.
 * An expression like names[nn++] decays to a pointer (the row's address) in
 * a real function call, but the former emitter generated a VALUE-context
 * dereference instead - found via tests/pint.c and tests/adaint.c, both
 * of which build an identifier table with exactly this
 * strcpy(names[nn++], text) shape, and both crashed ("not-implemented z80
 * instruction 0xdd") building their own symbol tables before this fix. */

char names[10][16];
int nn;

int main(void)
{
    strcpy(names[nn++], "identifier");
    strcpy(names[nn++], "second");
    strcpy(names[nn++], "identifier");

    printf("nn=%d [%s][%s][%s]\n", nn, names[0], names[1], names[2]);

    return 0;
}
