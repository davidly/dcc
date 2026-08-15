/* tfopenw.c - BDOS 15 (open file) is officially silent on ambiguous FCBs:
 * only search-first/next (17/18) and delete (19) are documented as
 * supporting '?' wildcards (CP/M 2.2 Interface Guide; also
 * https://www.seasip.info/Cpm/bdos.html). But CP/M 2.2's own BDOS.ASM
 * source shows open() internally reuses the very same directory-search
 * primitive delete does, with no check rejecting an ambiguous FCB - an
 * implementation accident, never a documented promise. That accident
 * produces a genuine, confirmed split among real emulators: ntvcm, cpmemu,
 * and zxcc reject an ambiguous open (NULL); tnylpo and cpm.exe silently
 * open whatever the first matching directory entry happens to be.
 *
 * Neither side is "wrong" - the spec never promises either outcome - so
 * this is deliberately left as documented, implementation-defined
 * behavior rather than "fixed" to match one camp: doing so would just be
 * guessing which undocumented accident the caller should be able to rely
 * on. Reported rather than asserted; don't rely on this in portable code.
 */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    FILE *f, *g, *h;

    unlink("OW1.TMP");
    unlink("OW2.TMP");
    f = fopen("OW1.TMP", "w"); fputs("first", f); fclose(f);
    g = fopen("OW2.TMP", "w"); fputs("second", g); fclose(g);

    h = fopen("OW?.TMP", "r");
    printf("REPORT fopen(\"OW?.TMP\",\"r\") returned %s\n", h ? "non-NULL" : "NULL");
    if (h) {
        char buf[16];
        int n = (int) fread(buf, 1, sizeof(buf) - 1, h);
        buf[n] = 0;
        printf("REPORT content read: [%s]\n", buf);
        fclose(h);
    }

    unlink("OW1.TMP");
    unlink("OW2.TMP");

    printf("tfopenw ok\n");
    return 0;
}
