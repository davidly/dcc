/* Dedicated semantic controls for the memory exercise runner schedule. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef MEM27_SMALL_ARRAY
#define MEM27_ALLOCS 65
#else
#define MEM27_ALLOCS 66
#endif

#ifdef MEM27_VOLATILE_LOGGING
volatile int memory27_logging = 1;
#else
int memory27_logging = 1;
#endif

void mchk27(char *p, int v, size_t c)
{
    register unsigned char *pc = (unsigned char *)p;
    unsigned char value = (unsigned char)(v & 0xff);
    size_t i;

    if (p == 0) {
        printf("request to chkmem a null pointer\n");
        exit(1);
    }
    for (i = 0; i < c; i++) {
        if (*pc != value) {
            printf(
                "memory isn't as expected! p %u, v %d, c %d, *pc %d\n",
                p, v, c, *pc);
            exit(1);
        }
        pc++;
    }
}

#ifdef MEM27_FAST_CHECK
extern void __fastcall fchk27(char *p, int v, size_t c);
#asm
        extrn   _mchk27
        public  _fchk27
_fchk27:
        push    bc
        push    de
        push    hl
        call    _mchk27
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#define MEM27_CHECK fchk27
#else
#define MEM27_CHECK mchk27
#endif

#ifdef MEM27_RENAMED_CALLS
static void *memory27_allocate(size_t size)
{
    return malloc(size);
}

static void *memory27_allocate_zero(size_t count, size_t size)
{
    return calloc(count, size);
}

static void memory27_release(void *pointer)
{
    free(pointer);
}

#define MEM27_MALLOC memory27_allocate
#define MEM27_CALLOC memory27_allocate_zero
#define MEM27_FREE memory27_release
#else
#define MEM27_MALLOC malloc
#define MEM27_CALLOC calloc
#define MEM27_FREE free
#endif

#ifdef MEM27_STACK_MEMSET
static void *memory27_fill(void *pointer, int value, size_t count)
{
    unsigned char *bytes = (unsigned char *)pointer;

    while (count-- != 0)
        *bytes++ = (unsigned char)value;
    return pointer;
}
#define MEM27_MEMSET memory27_fill
#else
#define MEM27_MEMSET memset
#endif

#ifdef MEM27_RENAMED_LOCALS
#define i item_index
#define cb allocation_size
#define c_cb cleared_size
#define pc temporary_pointer
#define ap pointer_slots
#endif

#ifdef MEM27_SIGNED_INDEX
typedef int memory27_index_type;
#else
typedef size_t memory27_index_type;
#endif

#ifdef MEM27_GLOBAL_ARRAY
char *memory27_global_slots[MEM27_ALLOCS];
#endif

int main(int argc, char *argv[])
{
    memory27_index_type i;
    size_t cb, c_cb;
    char *pc;
#ifdef MEM27_VOLATILE_ARRAY
    static char * volatile ap[MEM27_ALLOCS];
#elif defined(MEM27_GLOBAL_ARRAY)
#define ap memory27_global_slots
#else
    static char *ap[MEM27_ALLOCS];
#endif
    memory27_logging = (argc > 1);
    pc = argv[0];

    for (size_t j = 0; j < 10; j++) {
        if (memory27_logging)
            printf("in alloc mode\n");

        for (i = 0; i < MEM27_ALLOCS; i++) {
#ifdef MEM27_SCALE_ELEVEN
            cb = 8 + (i * 11);
#else
            cb = 8 + (i * 10);
#endif
#ifdef MEM27_EXTRA_NINE
            c_cb = cb + 9;
#else
            c_cb = cb + 5;
#endif
            if (memory27_logging)
                printf("  i, cb: %d %d\n", i, cb);

#ifdef MEM27_SWAP_CALLOC
            pc = (char *)MEM27_CALLOC(1, c_cb);
#else
            pc = (char *)MEM27_CALLOC(c_cb, 1);
#endif
            MEM27_CHECK(pc, 0, c_cb);
            MEM27_MEMSET(pc, 0xcc, c_cb);

            ap[i] = (char *)MEM27_MALLOC(cb);
            MEM27_MEMSET(ap[i], 0xaa, cb);

            MEM27_CHECK(pc, 0xcc, c_cb);
            MEM27_FREE(pc);
        }

        if (memory27_logging)
            printf("in free mode, even first\n");

        for (i = 0; i < MEM27_ALLOCS; i += 2) {
            cb = 8 + (i * 10);
            c_cb = cb + 3;
            if (memory27_logging)
                printf("  i, cb: %d %d\n", i, cb);

            pc = (char *)MEM27_CALLOC(c_cb, 1);
            MEM27_CHECK(pc, 0, c_cb);
            MEM27_MEMSET(pc, 0xcc, c_cb);

            MEM27_CHECK(ap[i], 0xaa, cb);
            MEM27_MEMSET(ap[i], 0xff, cb);
#ifndef MEM27_SWAP_FREE_ORDER
            MEM27_FREE(ap[i]);
            MEM27_CHECK(pc, 0xcc, c_cb);
            MEM27_FREE(pc);
#else
            MEM27_CHECK(pc, 0xcc, c_cb);
            MEM27_FREE(pc);
            MEM27_FREE(ap[i]);
#endif
        }

        if (memory27_logging)
            printf("in free mode, now odd\n");

        for (i = 1; i < MEM27_ALLOCS; i += 2) {
            cb = 8 + (i * 10);
            c_cb = cb + 7;
            if (memory27_logging)
                printf("  i, cb: %d %d\n", i, cb);

            pc = (char *)MEM27_CALLOC(c_cb, 1);
            MEM27_CHECK(pc, 0, c_cb);
            MEM27_MEMSET(pc, 0xcc, c_cb);

            MEM27_CHECK(ap[i], 0xaa, cb);
            MEM27_MEMSET(ap[i], 0xff, cb);
#ifndef MEM27_SWAP_FREE_ORDER
            MEM27_FREE(ap[i]);
            MEM27_CHECK(pc, 0xcc, c_cb);
            MEM27_FREE(pc);
#else
            MEM27_CHECK(pc, 0xcc, c_cb);
            MEM27_FREE(pc);
            MEM27_FREE(ap[i]);
#endif
        }
    }

    printf("memory27 success\n");
#ifdef MEM27_RETURN_ONE
    return 1;
#else
    return 0;
#endif
}
