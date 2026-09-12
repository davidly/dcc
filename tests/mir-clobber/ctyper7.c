#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRW7_GUARD0 0xa5
#define CRW7_GUARD1 0x5a

static int failures;
static int ctype_calls;
static unsigned int ctype_hash;
static unsigned char *owned_base;
static unsigned int owned_size;
static int allocation_calls;
static int resize_calls;
static int free_calls;
static int allocation_failures;
static int resize_failures;
static int force_allocation_failure;
static int force_resize_failure;
static unsigned int allocation_width_oracle;
static unsigned int resize_width_oracle;

static int crw7_alpha_body(int value);
static void crw7_check_body(int condition, const char *name);
static void *crw7_allocate_body(unsigned int size);
static char *crw7_copy_body(char *destination, const char *source);
static void *crw7_resize_body(void *pointer, unsigned int size);
static int crw7_compare_body(const char *left, const char *right);
static void crw7_release_body(void *pointer);
extern unsigned int cw7dsp(
    unsigned int operation, unsigned int first, unsigned int second);

static void crw7_fail(const char *name)
{
    printf("FAIL %s\n", name);
    ++failures;
}

static int crw7_guards_ok(void)
{
    if (owned_base == NULL)
        return 1;
    return owned_base[0] == CRW7_GUARD0 &&
        owned_base[1] == CRW7_GUARD1 &&
        owned_base[owned_size + 2] == CRW7_GUARD1 &&
        owned_base[owned_size + 3] == CRW7_GUARD0;
}

static void crw7_set_guards(void)
{
    owned_base[0] = CRW7_GUARD0;
    owned_base[1] = CRW7_GUARD1;
    owned_base[owned_size + 2] = CRW7_GUARD1;
    owned_base[owned_size + 3] = CRW7_GUARD0;
}

static int crw7_note_ctype(int input, int result)
{
    ++ctype_calls;
    ctype_hash = ctype_hash * 33U +
        ((unsigned int)input & 0xffffU) +
        ((unsigned int)result & 0xffU);
    return result;
}

unsigned int cw7dsp(
    unsigned int operation, unsigned int first, unsigned int second)
{
    switch (operation) {
    case 1:
        return (unsigned int)crw7_alpha_body((int)first);
    case 2:
        crw7_check_body((int)first, (const char *)second);
        return 0;
    case 3:
        return (unsigned int)crw7_allocate_body(first);
    case 4:
        return (unsigned int)crw7_copy_body(
            (char *)first, (const char *)second);
    case 5:
        return (unsigned int)crw7_resize_body(
            (void *)first, second);
    case 6:
        return (unsigned int)crw7_compare_body(
            (const char *)first, (const char *)second);
    case 7:
        crw7_release_body((void *)first);
        return 0;
    }
    return 0;
}

static int crw7_alpha_body(int value)
{
    return crw7_note_ctype(value, isalpha(value));
}

#ifdef CRW7_FAST_CTYPE
extern int __fastcall crw7_alpha(int value);
#asm
_crw7_alpha:
        ld      bc,0
        push    bc
        push    hl
        ld      hl,1
        push    hl
        call    _cw7dsp
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
static int crw7_alpha(int value)
{
    return (int)cw7dsp(1, (unsigned int)value, 0);
}
#endif

static int crw7_alnum(int value)
{
    return crw7_note_ctype(value, isalnum(value));
}

static int crw7_digit(int value)
{
    return crw7_note_ctype(value, isdigit(value));
}

static int crw7_space(int value)
{
    return crw7_note_ctype(value, isspace(value));
}

static int crw7_upper(int value)
{
    return crw7_note_ctype(value, isupper(value));
}

static int crw7_lower(int value)
{
    return crw7_note_ctype(value, islower(value));
}

static int crw7_xdigit(int value)
{
    return crw7_note_ctype(value, isxdigit(value));
}

static int crw7_print(int value)
{
    return crw7_note_ctype(value, isprint(value));
}

static int crw7_cntrl(int value)
{
    return crw7_note_ctype(value, iscntrl(value));
}

static int crw7_punct(int value)
{
    return crw7_note_ctype(value, ispunct(value));
}

static int crw7_toupper(int value)
{
    return crw7_note_ctype(value, toupper(value));
}

static int crw7_tolower(int value)
{
    return crw7_note_ctype(value, tolower(value));
}

static void crw7_check_body(int condition, const char *name)
{
    if (!condition)
        crw7_fail(name);
}

#ifdef CRW7_FAST_CHECK
extern void __fastcall crw7_check(int condition, const char *name);
#asm
_crw7_check:
        push    de
        push    hl
        ld      hl,2
        push    hl
        call    _cw7dsp
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
static void crw7_check(int condition, const char *name)
{
    cw7dsp(
        2, (unsigned int)condition, (unsigned int)name);
}
#endif

static void *crw7_allocate_body(unsigned int size)
{
    ++allocation_calls;
    allocation_width_oracle |= size;
    if (force_allocation_failure) {
        ++allocation_failures;
        return NULL;
    }
    if (owned_base != NULL) {
        crw7_fail("allocate ownership");
        return NULL;
    }
    owned_base = (unsigned char *)malloc(size + 4U);
    if (owned_base == NULL)
        return NULL;
    owned_size = size;
    crw7_set_guards();
    return owned_base + 2;
}

#ifdef CRW7_FAST_ALLOC
extern void *__fastcall crw7_allocate(unsigned int size);
#asm
_crw7_allocate:
        ld      de,0
        push    de
        push    hl
        ld      hl,3
        push    hl
        call    _cw7dsp
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
static void *crw7_allocate(unsigned int size)
{
    return (void *)cw7dsp(3, size, 0);
}
#endif

static char *crw7_copy_body(char *destination, const char *source)
{
    if (destination != (char *)(owned_base + 2))
        crw7_fail("copy ownership");
    return strcpy(destination, source);
}

#ifdef CRW7_FAST_COPY
extern char *__fastcall crw7_copy(
    char *destination, const char *source);
#asm
_crw7_copy:
        push    de
        push    hl
        ld      hl,4
        push    hl
        call    _cw7dsp
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
static char *crw7_copy(char *destination, const char *source)
{
    return (char *)cw7dsp(
        4, (unsigned int)destination, (unsigned int)source);
}
#endif

static void *crw7_resize_body(
    void *pointer, unsigned int size)
{
    unsigned char *resized;

    ++resize_calls;
    resize_width_oracle |= size;
    if (pointer != (char *)(owned_base + 2) ||
        !crw7_guards_ok()) {
        crw7_fail("resize ownership");
        return NULL;
    }
    if (force_resize_failure) {
        ++resize_failures;
        return NULL;
    }
    resized = (unsigned char *)realloc(owned_base, size + 4U);
    if (resized == NULL)
        return NULL;
    owned_base = resized;
    owned_size = size;
    crw7_set_guards();
    return owned_base + 2;
}

#ifdef CRW7_FAST_RESIZE
extern void *__fastcall crw7_resize(
    void *pointer, unsigned int size);
#asm
_crw7_resize:
        push    de
        push    hl
        ld      hl,5
        push    hl
        call    _cw7dsp
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
static void *crw7_resize(void *pointer, unsigned int size)
{
    return (void *)cw7dsp(
        5, (unsigned int)pointer, size);
}
#endif

static int crw7_compare_body(
    const char *left, const char *right)
{
    if (left != (char *)(owned_base + 2) ||
        !crw7_guards_ok())
        crw7_fail("compare bounds");
    return strcmp(left, right);
}

#ifdef CRW7_FAST_COMPARE
extern int __fastcall crw7_compare(
    const char *left, const char *right);
#asm
_crw7_compare:
        push    de
        push    hl
        ld      hl,6
        push    hl
        call    _cw7dsp
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
static int crw7_compare(const char *left, const char *right)
{
    return (int)cw7dsp(
        6, (unsigned int)left, (unsigned int)right);
}
#endif

static void crw7_release_body(void *pointer)
{
    ++free_calls;
    if (pointer != (char *)(owned_base + 2) ||
        !crw7_guards_ok()) {
        crw7_fail("free ownership");
        return;
    }
    free(owned_base);
    owned_base = NULL;
    owned_size = 0;
}

#ifdef CRW7_FAST_FREE
extern void __fastcall crw7_release(void *pointer);
#asm
_crw7_release:
        ld      de,0
        push    de
        push    hl
        ld      hl,7
        push    hl
        call    _cw7dsp
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
static void crw7_release(void *pointer)
{
    cw7dsp(7, (unsigned int)pointer, 0);
}
#endif

static int crw7_exact(void)
{
#ifdef CRW21_VOLATILE_POINTER
    volatile char *p;
#else
    char *p;
#endif

    crw7_check(crw7_alpha('A') != 0, "isalpha A");
    crw7_check(crw7_alpha('z') != 0, "isalpha z");
    crw7_check(crw7_alpha('7') == 0, "isalpha 7");
    crw7_check(crw7_alnum('7') != 0, "isalnum 7");
    crw7_check(crw7_digit('9') != 0, "isdigit 9");
    crw7_check(crw7_space('\n') != 0, "isspace newline");
    crw7_check(crw7_upper('Q') != 0, "isupper Q");
    crw7_check(crw7_lower('q') != 0, "islower q");
    crw7_check(crw7_xdigit('f') != 0, "isxdigit f");
    crw7_check(crw7_print(' ') != 0, "isprint space");
    crw7_check(crw7_cntrl('\r') != 0, "iscntrl cr");
    crw7_check(crw7_punct('!') != 0, "ispunct bang");
    crw7_check(crw7_toupper('m') == 'M', "toupper m");
    crw7_check(crw7_tolower('M') == 'm', "tolower M");

    p = (char *)crw7_allocate(4);
    if (!p) {
        printf("FAIL malloc 4\n");
        return 1;
    }

    crw7_copy(p, "abc");

    p = (char *)crw7_resize(p, 8);
    if (!p) {
        printf("FAIL realloc grow null\n");
        return 1;
    }

    crw7_check(crw7_compare(p, "abc") == 0, "realloc grow preserve");

    p[3] = 'd';
    p[4] = 0;

    p = (char *)crw7_resize(p, 3);
    if (!p) {
        printf("FAIL realloc shrink null\n");
        return 1;
    }

    crw7_check(p[0] == 'a', "realloc shrink byte0");
    crw7_check(p[1] == 'b', "realloc shrink byte1");

    crw7_release(p);

    if (failures) {
        printf("ctype/realloc failed: %d\n", failures);
        return 1;
    }

    printf("ctype/realloc ok\n");
    return 0;
}

int main(void)
{
    char *p;
    char *resized;
    int exact_result;

    exact_result = crw7_exact();
    if (exact_result != 0)
        return exact_result;
    if (owned_base != NULL || allocation_calls != 1 ||
        resize_calls != 2 || free_calls != 1 ||
        allocation_width_oracle != 4U ||
        resize_width_oracle != 11U ||
        ctype_calls != 14)
        crw7_fail("exact ownership/width");

    force_allocation_failure = 1;
    if (crw7_allocate(0x8001U) != NULL ||
        allocation_failures != 1 ||
        owned_base != NULL)
        crw7_fail("allocation failure");
    force_allocation_failure = 0;

    p = (char *)crw7_allocate(5);
    if (p == NULL)
        crw7_fail("failure setup");
    else {
        p[0] = (char)0x81;
        p[1] = 'B';
        p[2] = 'C';
        p[3] = 'D';
        p[4] = (char)0xfe;
        force_resize_failure = 1;
        resized = (char *)crw7_resize(p, 0x8003U);
        force_resize_failure = 0;
        if (resized != NULL || resize_failures != 1 ||
            owned_base == NULL ||
            (unsigned char)p[0] != 0x81U ||
            p[1] != 'B' || p[2] != 'C' || p[3] != 'D' ||
            (unsigned char)p[4] != 0xfeU ||
            !crw7_guards_ok())
            crw7_fail("realloc failure preserve");
        crw7_release(p);
    }

    if (owned_base != NULL || free_calls != 2 ||
        allocation_calls != 3 || resize_calls != 3 ||
        allocation_width_oracle != 0x8005U ||
        resize_width_oracle != 0x800bU)
        crw7_fail("final ownership/width");

    printf(
        "CRW7 oracle failures=%d ctype=%d hash=%u "
        "alloc=%d/%d resize=%d/%d free=%d live=%d "
        "width=%u,%u bounds=%d\n",
        failures, ctype_calls, ctype_hash,
        allocation_calls, allocation_failures,
        resize_calls, resize_failures, free_calls,
        owned_base != NULL,
        allocation_width_oracle, resize_width_oracle,
        crw7_guards_ok());
    return failures != 0;
}
