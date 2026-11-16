/* Console cursor, guard-byte and register-preservation regression checks. */
#include <stdio.h>
#include <string.h>

unsigned int cbyte;
int preserved = 1;
extern void emit(void);

#asm
        extrn   __conout
        public  _emit
_emit:
        ld      a,(_cbyte)
        ld      e,a
        ld      d,66h
        ld      bc,55aah
        ld      hl,7788h
        call    __conout
        ld      a,b
        cp      55h
        jr      nz,co_bad
        ld      a,c
        cp      0aah
        jr      nz,co_bad
        ld      a,d
        cp      66h
        jr      nz,co_bad
        ld      a,(_cbyte)
        cp      e
        jr      nz,co_bad
        ld      a,h
        cp      77h
        jr      nz,co_bad
        ld      a,l
        cp      88h
        ret     z
co_bad:
        ld      hl,0
        ld      (_preserved),hl
        ret
#endasm

static void send(unsigned int c)
{
    cbyte = c;
    emit();
}

int main(void)
{
    static const unsigned int capacities[] = { 1, 2, 7, 257, 513 };
    static char storage[516];
    char *buf = storage + 1;
    unsigned int mode, n, cap, i;
    int failures = 0;

    for (mode = 0; mode < 3; ++mode) {
        for (n = 0; n < 5; ++n) {
            cap = capacities[n];
            printf("mode %u capacity %u: ", mode, cap);
            fflush(stdout);
            memset(storage, '?', sizeof storage);
            storage[0] = '<';
            buf[cap] = '!';             /* guard borrowed during a flush */
            buf[cap + 1] = '>';
            if (setvbuf(stdout, buf, mode, cap + 1) != 0)
                ++failures;

            /* At capacities 257/513 the first byte has the same low address
             * as the end, but a different high byte. It must stay buffered. */
            for (i = 0; i < cap - 1; ++i)
                send('A');
            if (mode != _IONBF) {
                for (i = 0; i < cap - 1; ++i)
                    if (buf[i] != 'A')
                        ++failures;
            }

            /* In buffered modes CR fills the buffer and LF starts the next.
             * Unbuffered CR/LF share one flush unless capacity is one. */
            send('\n');
            fflush(stdout);
            if (buf[mode == _IONBF && cap > 1 ? 1 : 0] != '\n' ||
                buf[cap] != '!' ||
                storage[0] != '<' || buf[cap + 1] != '>')
                ++failures;

            /* Leading, consecutive, embedded and final '$', including a '$'
             * that is the last byte of a full buffer. Check cursor reset. */
            send('$');
            send('$');
            send('x');
            send('$');
            fflush(stdout);
            send('\n');
            fflush(stdout);
            if (buf[cap] != '!' || storage[0] != '<' || buf[cap + 1] != '>')
                ++failures;
            setvbuf(stdout, (char *)0, _IOLBF, 0);
        }
    }

    /* Too-small caller buffers must fall back without touching the caller. */
    for (i = 0; i < 2; ++i) {
        buf[0] = '?';
        setvbuf(stdout, buf, _IOFBF, i);
        send('F');
        send('\n');
        fflush(stdout);
        if (buf[0] != '?')
            ++failures;
    }
    setvbuf(stdout, (char *)0, _IOLBF, 0);
    printf("console: %d failures, registers %s\n", failures,
           preserved ? "OK" : "FAIL");
    return failures != 0 || !preserved;
}
