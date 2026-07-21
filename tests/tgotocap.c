#include <stdio.h>

/* The parser supports MAX_USER_LABELS (256). The loop-exit analysis must not
 * truncate its internal-label set below that limit and mistake L65 for an
 * escaping target. */
static int internal_label_loop(void)
{
    while (1) {
L01: L02: L03: L04: L05: L06: L07: L08:
L09: L10: L11: L12: L13: L14: L15: L16:
L17: L18: L19: L20: L21: L22: L23: L24:
L25: L26: L27: L28: L29: L30: L31: L32:
L33: L34: L35: L36: L37: L38: L39: L40:
L41: L42: L43: L44: L45: L46: L47: L48:
L49: L50: L51: L52: L53: L54: L55: L56:
L57: L58: L59: L60: L61: L62: L63: L64:
L65:
        goto L65;
    }
}

/* Runtime coverage: forward and backward goto branches that must produce a
 * deterministic result on the target. This exercises actual label-target
 * codegen (jumps taken at run time), not just the parser's label-capacity
 * limit that internal_label_loop() stresses at compile time. */
static int goto_runtime(void)
{
    int i;
    int sum;

    i = 0;
    sum = 0;
loop:
    i = i + 1;
    if (i > 10)
        goto done;
    if (i == 5)
        goto skip; /* forward jump: exclude 5 from the sum */
    sum = sum + i;
    goto loop; /* backward jump */
skip:
    goto loop;
done:
    return sum; /* 1+2+3+4+6+7+8+9+10 = 50 */
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc == 0)
        return internal_label_loop();
    printf("tgotocap ok runtime=%d\n", goto_runtime());
    return 0;
}
