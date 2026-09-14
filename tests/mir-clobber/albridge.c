#include <stdio.h>
#include <stdlib.h>

#ifdef ALLOCBRIDGE_VOLATILE_FIRST
#define ALLOCBRIDGE_FIRST_QUAL volatile
#else
#define ALLOCBRIDGE_FIRST_QUAL
#endif

#ifdef ALLOCBRIDGE_VOLATILE_MIDDLE
#define ALLOCBRIDGE_MIDDLE_QUAL volatile
#else
#define ALLOCBRIDGE_MIDDLE_QUAL
#endif

#ifdef ALLOCBRIDGE_VOLATILE_LAST
#define ALLOCBRIDGE_LAST_QUAL volatile
#else
#define ALLOCBRIDGE_LAST_QUAL
#endif

#ifdef ALLOCBRIDGE_VOLATILE_MERGED
#define ALLOCBRIDGE_MERGED_QUAL volatile
#else
#define ALLOCBRIDGE_MERGED_QUAL
#endif

#ifdef ALLOCBRIDGE_RENAMED
#define allocator_bridge_kernel allocator_bridge_renamed
#endif

static unsigned int observed_size;
static int observed_value;
static unsigned char observed_first;
static unsigned char observed_middle;
static unsigned char observed_last;

#ifdef ALLOCBRIDGE_FAST_FAILURE
void allocator_bridge_fail(const char *message)
#else
static void allocator_bridge_fail(const char *message)
#endif
{
    printf("allocator bridge failure: %s\n", message);
    exit(1);
}

#ifdef ALLOCBRIDGE_FAST_FILL
void allocator_bridge_fill(
    unsigned char *pointer, unsigned int size, int value)
#else
static void allocator_bridge_fill(
    unsigned char *pointer, unsigned int size, int value)
#endif
{
    unsigned int index;

    for (index = 0; index < size; ++index)
        pointer[index] = (unsigned char)value;
    observed_size = size;
    observed_value = value;
    observed_first = pointer[0];
    observed_middle = pointer[size / 2U];
    observed_last = pointer[size - 1U];
}

#ifdef ALLOCBRIDGE_FAST_ALLOCATE
extern void *__fastcall allocator_bridge_allocate(unsigned int size);
#define ALLOCBRIDGE_ALLOCATE(size) allocator_bridge_allocate(size)
#else
#define ALLOCBRIDGE_ALLOCATE(size) malloc(size)
#endif

#ifdef ALLOCBRIDGE_FAST_FREE
extern void __fastcall allocator_bridge_release(void *pointer);
#define ALLOCBRIDGE_RELEASE(pointer) allocator_bridge_release(pointer)
#else
#define ALLOCBRIDGE_RELEASE(pointer) free(pointer)
#endif

#ifdef ALLOCBRIDGE_FAST_FAILURE
extern void __fastcall allocator_bridge_fast_fail(const char *message);
#define ALLOCBRIDGE_FAILURE(message) allocator_bridge_fast_fail(message)
#else
#define ALLOCBRIDGE_FAILURE(message) allocator_bridge_fail(message)
#endif

#ifdef ALLOCBRIDGE_FAST_FILL
extern void __fastcall allocator_bridge_fast_fill(
    unsigned char *pointer, unsigned int size, int value);
#define ALLOCBRIDGE_FILL(pointer, size, value) \
    allocator_bridge_fast_fill(pointer, size, value)
#else
#define ALLOCBRIDGE_FILL(pointer, size, value) \
    allocator_bridge_fill(pointer, size, value)
#endif

static void allocator_bridge_kernel(void)
{
    unsigned char *ALLOCBRIDGE_FIRST_QUAL first;
    unsigned char *ALLOCBRIDGE_MIDDLE_QUAL middle;
    unsigned char *ALLOCBRIDGE_LAST_QUAL last;
    unsigned char *ALLOCBRIDGE_MERGED_QUAL merged;

    first = (unsigned char *)ALLOCBRIDGE_ALLOCATE(1000U);
    middle = (unsigned char *)ALLOCBRIDGE_ALLOCATE(1000U);
    last = (unsigned char *)ALLOCBRIDGE_ALLOCATE(1000U);
    if (first == 0 || middle == 0 || last == 0)
        ALLOCBRIDGE_FAILURE("setup allocation");

    ALLOCBRIDGE_RELEASE(first);
    ALLOCBRIDGE_RELEASE(last);
    ALLOCBRIDGE_RELEASE(middle);
    merged = (unsigned char *)ALLOCBRIDGE_ALLOCATE(3006U);
    if (merged != first)
        ALLOCBRIDGE_FAILURE("coalesced address");
#ifdef ALLOCBRIDGE_EXTRA_CFG
    if (merged == middle)
        ALLOCBRIDGE_FAILURE("unexpected middle address");
#endif
    ALLOCBRIDGE_FILL(merged, 3006U, 0x5a);
    ALLOCBRIDGE_RELEASE(merged);
}

int main(void)
{
    int failures = 0;

    allocator_bridge_kernel();
    if (observed_size != 3006U || observed_value != 0x5a ||
        observed_first != 0x5a || observed_middle != 0x5a ||
        observed_last != 0x5a)
        ++failures;
    printf("allocator bridge size=%u value=%d samples=%u,%u,%u failures=%d\n",
           observed_size, observed_value, observed_first, observed_middle,
           observed_last, failures);
    return failures != 0;
}

#if defined(ALLOCBRIDGE_FAST_ALLOCATE) && defined(_DCC_)
#asm
        extrn   _malloc
_allocator_bridge_allocate:
        push    hl
        call    _malloc
        pop     bc
        ret
#endasm
#endif

#if defined(ALLOCBRIDGE_FAST_FAILURE) && defined(_DCC_)
#asm
_allocator_bridge_fast_fail:
        push    hl
        call    _allocator_bridge_fail
        pop     de
        ret
#endasm
#endif

#if defined(ALLOCBRIDGE_FAST_FILL) && defined(_DCC_)
#asm
_allocator_bridge_fast_fill:
        push    bc
        push    de
        push    hl
        call    _allocator_bridge_fill
        ld      hl,6
        add     hl,sp
        ld      sp,hl
        ret
#endasm
#endif

#if defined(ALLOCBRIDGE_FAST_FREE) && defined(_DCC_)
#asm
        extrn   _free
_allocator_bridge_release:
        push    hl
        call    _free
        pop     bc
        ret
#endasm
#endif
