/* Dedicated semantic controls for the non-local descent and runner schedules. */

#include <stdio.h>
#include <setjmp.h>

#ifdef NL28_VOLATILE_ENV
static volatile jmp_buf nl28_env;
#else
static jmp_buf nl28_env;
#endif

#ifdef NL28_VOLATILE_FAILURES
static volatile int nl28_failures;
#else
static int nl28_failures;
#endif

#ifdef NL28_FAST_SAVE
extern int __fastcall nl28_fast_save(jmp_buf environment);
#asm
        extrn   _setjmp
        public  _nl28_fast_save
_nl28_fast_save:
        pop     de
        ; The saved SP may be reused before longjmp returns here.
        ld      (_nl28_fast_save_return),de
        push    hl
        ld      de,_nl28_fast_save_cleanup
        push    de
        jp      _setjmp
_nl28_fast_save_cleanup:
        pop     bc
        ld      de,(_nl28_fast_save_return)
        push    de
        ret
_nl28_fast_save_return:
        dw      0
#endasm
#define NL28_SETJMP nl28_fast_save
#else
#define NL28_SETJMP setjmp
#endif

#ifdef NL28_FAST_JUMP
extern _Noreturn void __fastcall nl28_fast_jump(
    jmp_buf environment, int value);
#asm
        extrn   _longjmp
        public  _nl28_fast_jump
_nl28_fast_jump:
        pop     bc
        push    de
        push    hl
        push    bc
        jp      _longjmp
#endasm
#define NL28_LONGJMP nl28_fast_jump
#else
#define NL28_LONGJMP longjmp
#endif

#ifdef NL28_VOLATILE_LOCALS
#define NL28_LOCAL_QUALIFIER volatile
#else
#define NL28_LOCAL_QUALIFIER
#endif

#ifdef NL28_JUMP_41
#define NL28_JUMP_VALUE 41
#else
#define NL28_JUMP_VALUE 42
#endif

#ifdef NL28_UNSIGNED_LEVEL
typedef unsigned int nl28_level_type;
#else
typedef int nl28_level_type;
#endif

static long nl28_descent(
    nl28_level_type level, int first, int second, int third)
{
    NL28_LOCAL_QUALIFIER long local[4];
    int index;

    local[0] = first + level;
    local[1] = second + level;
    local[2] = third + level;
    local[3] = first + second + third + level;

    if (level == 0) {
        NL28_LONGJMP(nl28_env, NL28_JUMP_VALUE);
        return -1;
    }

    for (index = 0; index < 4; index++)
        local[index] += nl28_descent(
            level - 1, first + 1, second + 2, third + 3);

    return local[0] + local[1] + local[2] + local[3];
}

void nck28(const char *name, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        nl28_failures++;
    }
}

#ifdef NL28_FAST_CHECK
extern void __fastcall fck28(
    const char *name, int condition);
#asm
        extrn   _nck28
        public  _fck28
_fck28:
        push    de
        push    hl
        call    _nck28
        pop     bc
        pop     bc
        ret
#endasm
#define NL28_CHECK fck28
#else
#define NL28_CHECK nck28
#endif

#ifdef NL28_CYCLES_FOUR
#define NL28_CYCLE_COUNT 4
#else
#define NL28_CYCLE_COUNT 5
#endif

#ifdef NL28_MARKER_900
#define NL28_MARKER_BASE 900
#else
#define NL28_MARKER_BASE 1000
#endif

#ifdef NL28_DEPTH_SEVEN
#define NL28_DEPTH 7
#else
#define NL28_DEPTH 8
#endif

int main(void)
{
    volatile int cycle;
    int result;

    nl28_failures = 0;

    for (cycle = 0; cycle < NL28_CYCLE_COUNT; cycle++) {
        int marker = NL28_MARKER_BASE + cycle;

        result = NL28_SETJMP(nl28_env);
        if (result == 0) {
            NL28_CHECK(
                "marker before jump",
                marker == NL28_MARKER_BASE + cycle);
            nl28_descent(NL28_DEPTH, 1, 2, 3);
            NL28_CHECK("unreachable after descent", 0);
        } else {
            NL28_CHECK(
                "longjmp value", result == NL28_JUMP_VALUE);
            NL28_CHECK(
                "marker after jump",
                marker == NL28_MARKER_BASE + cycle);
        }
    }

    if (nl28_failures) {
        printf("nonlocal28 FAILED: %d\n", nl28_failures);
        return 1;
    }
    printf("nonlocal28: all tests passed\n");
#ifdef NL28_RETURN_TWO
    return 2;
#else
    return 0;
#endif
}
