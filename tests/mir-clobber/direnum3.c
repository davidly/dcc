#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>

#define DEFAULT_DMA 128

struct FCBCPM {
    uint8_t dr;
    char n[8];
    char t[3];
    uint8_t ex;
    uint8_t s1;
    uint8_t s2;
    uint8_t rc;
    uint8_t d[16];
    uint8_t cr;
    uint8_t r0;
    uint8_t r1;
    uint8_t r2;
};

static int initialize_calls;
static int find_first_calls;
static int find_next_calls;
static int size_bdos_calls;
static int duplicate_calls;
static int sort_calls;
static int search_calls;
static int print_calls;
static int size_calls;
static int free_calls;

bool quiet = false;

bool fcb_initialize(struct FCBCPM *pfcb, char *pfilename)
{
    char *pdot;
    char *ptype;
    int len;
    int nlen;
    int tlen;

    ++initialize_calls;
    memset(pfcb, 0, sizeof(struct FCBCPM));
    memset(pfcb->n, ' ', 8);
    memset(pfcb->t, ' ', 3);

    len = strlen(pfilename);
    pdot = strchr(pfilename, '.');
    if (pdot) {
        nlen = (int)(pdot - pfilename);
        if (nlen > 8)
            return false;
        memcpy(pfcb->n, pfilename, nlen);
        ptype = pdot + 1;
        tlen = strlen(ptype);
        if (tlen > 3)
            return false;
        memcpy(pfcb->t, ptype, tlen);
    } else {
        if (len > 8)
            return false;
        memcpy(pfcb->n, pfilename, len);
    }
    return true;
}

static int tracked_bdos(int fn, int dearg)
{
    if (fn == 17)
        ++find_first_calls;
    else if (fn == 18)
        ++find_next_calls;
    else if (fn == 35)
        ++size_bdos_calls;
    return bdos(fn, dearg);
}

static char *tracked_duplicate(char *text)
{
    ++duplicate_calls;
    return strdup(text);
}

static void tracked_sort(
    void *base, size_t count, size_t width,
    int (*compare)(const void *, const void *))
{
    ++sort_calls;
    qsort(base, count, width, compare);
}

static void *tracked_search(
    const void *key, const void *base, size_t count, size_t width,
    int (*compare)(const void *, const void *))
{
    ++search_calls;
    return bsearch(key, base, count, width, compare);
}

static int tracked_print(const char *format, ...)
{
    va_list arguments;
    int result;

    ++print_calls;
    va_start(arguments, format);
    result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

static void tracked_free(void *pointer)
{
    ++free_calls;
    free(pointer);
}

static uint32_t tracked_size(char *pfilename)
{
    struct FCBCPM size_fcb;
    uint32_t size = 0;
    int result;

    ++size_calls;
    fcb_initialize(&size_fcb, pfilename);
    result = tracked_bdos(35, &size_fcb);
    if (result == 0)
        size = (uint32_t)size_fcb.r0 +
            (((uint32_t)size_fcb.r1) << 8);
    return size << 7;
}

int do_compare(char **a, char **b)
{
    return strcmp(*a, *b);
}

int enumerate(char *pfile)
{
    char *pappname = "Q7BETA22.D";
    struct FCBCPM the_fcb;
    struct FCBCPM *result_fcb;
    int result, i, len;
    size_t list_len = 0;
    char *pthis, **presult;
    uint32_t fsize;
    char file[13];
    static char *list[400];

    if (!fcb_initialize(&the_fcb, pfile))
        return false;

    result = tracked_bdos(17, &the_fcb);

    while (result >= 0 && result <= 3) {
        result_fcb =
            (struct FCBCPM *)(DEFAULT_DMA + (result * 32));
        len = 0;

        for (i = 0; i < 8; i++) {
            if (' ' != result_fcb->n[i])
                file[len++] = result_fcb->n[i];
            else
                break;
        }

        if (' ' != result_fcb->t[0]) {
            file[len++] = '.';
            for (i = 0; i < 3; i++) {
                if (' ' != result_fcb->t[i])
                    file[len++] = result_fcb->t[i];
                else
                    break;
            }
        }

        file[len] = 0;
        list[list_len++] = tracked_duplicate(file);
        if ((sizeof(list) / sizeof(char *)) == list_len)
            break;

        result = tracked_bdos(18, &the_fcb);
    }

    if (255 != result) {
        tracked_print("unexpected directory result: %d\n", result);
        return false;
    }

    tracked_sort(list, list_len, sizeof(char *), do_compare);

    presult = tracked_search(
        &pappname, list, list_len, sizeof(char *), do_compare);
    if (0 != presult)
        tracked_print("found=%s\n", *presult);

    for (i = 0; i < list_len; i++) {
        fsize = tracked_size(list[i]);
        if (!quiet)
            tracked_print(
                "entry=%d name=%s size=%ld\n",
                i, list[i], (long)fsize);
        tracked_free(list[i]);
    }

    return true;
}

int main(void)
{
    int ok;

    ok = enumerate("Q7??????.D??");
    printf(
        "oracle ok=%d init=%d first=%d next=%d sizebdos=%d "
        "dup=%d sort=%d search=%d print=%d size=%d free=%d\n",
        ok, initialize_calls, find_first_calls, find_next_calls,
        size_bdos_calls, duplicate_calls, sort_calls, search_calls,
        print_calls, size_calls, free_calls);
    if (!ok ||
        initialize_calls != 4 ||
        find_first_calls != 1 ||
        find_next_calls != 3 ||
        size_bdos_calls != 3 ||
        duplicate_calls != 3 ||
        sort_calls != 1 ||
        search_calls != 1 ||
        print_calls != 4 ||
        size_calls != 3 ||
        free_calls != 3)
        return 1;
    return 0;
}
