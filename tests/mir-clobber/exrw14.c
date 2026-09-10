#include <stdio.h>
#include <stdlib.h>

#define RW14_DEPTH 40

#ifdef RW14_VOLATILE_FAILURES
static volatile int failures;
#else
static int failures;
#endif
static int exec_calls;
static int execv_calls;

static int rw14_exec(const char *name, const char *tail)
{
    ++exec_calls;
    return exec(name, tail);
}

static int rw14_execv(const char *name, char *const arguments[])
{
    ++execv_calls;
    return execv(name, arguments);
}

static int rw14_recurse(int depth, int marker, int use_execv)
{
    int local_check;
    int result;

    if (0 == depth) {
#ifdef RW14_VOLATILE_BASE_RESULT
        volatile int r;
#else
        int r;
#endif
        if (use_execv) {
            char *av[2];
            av[0] = "RW14MISS";
            av[1] = (char *)0;
            r = rw14_execv("RW14MISS", av);
        } else
            r = rw14_exec("RW14MISS", "");

        if (-1 != r) {
            printf("base result=%d\n", r);
            ++failures;
            return -999;
        }
        return marker;
    }

    local_check = marker + depth;
    result = rw14_recurse(depth - 1, marker, use_execv);
    if (result != marker) {
        printf(
            "result depth=%d marker=%d got=%d\n",
            depth, marker, result);
        ++failures;
        return -999;
    }
    if (local_check != marker + depth) {
        printf(
            "local depth=%d expected=%d got=%d\n",
            depth, marker + depth, local_check);
        ++failures;
        return -999;
    }
    return result;
}

int main(void)
{
    int first;
    int second;
    unsigned long oracle;

    first = rw14_recurse(RW14_DEPTH, 12345, 0);
    second = rw14_recurse(RW14_DEPTH, 22222, 1);
    oracle = (unsigned long)(unsigned int)first * 257UL;
    oracle += (unsigned long)(unsigned int)second * 17UL;
    oracle += (unsigned long)exec_calls * 11UL;
    oracle += (unsigned long)execv_calls * 13UL;
    oracle += (unsigned long)(unsigned int)failures * 23UL;
    printf(
        "RW14 oracle=%lu first=%d second=%d exec=%d execv=%d "
        "failures=%d\n",
        oracle, first, second, exec_calls, execv_calls,
        (int)failures);
    return failures ? 1 : 0;
}
