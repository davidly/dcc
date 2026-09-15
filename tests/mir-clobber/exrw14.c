#include <stdio.h>
#include <stdlib.h>

#define RW14_DEPTH 40

#ifdef RW64_RENAME_HELPERS
#define rw14_exec rw64_exec_alias
#define rw14_execv rw64_execv_alias
#endif
#ifdef RW64_RENAME_FAILURES
#define failures rw64_failure_count
#endif

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

#ifdef RW64_UNSIGNED_EXECV_RETURN
static unsigned int rw14_execv(
    const char *name, char *const arguments[])
#elif defined(RW64_EXECV_VOID_POINTERS)
static int rw14_execv(const char *name, void *arguments)
#else
static int rw14_execv(const char *name, char *const arguments[])
#endif
{
    ++execv_calls;
#ifdef RW64_UNSIGNED_EXECV_RETURN
    return (unsigned int)execv(name, arguments);
#elif defined(RW64_EXECV_VOID_POINTERS)
    return execv(name, (char **)arguments);
#else
    return execv(name, arguments);
#endif
}

#ifdef RW64_FIXED_REPORTS
static int rw14_base_report(const char *format, int value)
{
    return printf(format, value);
}

static int rw14_detail_report(
    const char *format, int first, int second, int third)
{
    return printf(format, first, second, third);
}
#define RW14_BASE_REPORT rw14_base_report
#define RW14_DETAIL_REPORT rw14_detail_report
#else
#define RW14_BASE_REPORT printf
#define RW14_DETAIL_REPORT printf
#endif

#ifdef RW64_UNSIGNED_MARKER
#define RW14_MARKER_TYPE unsigned int
#else
#define RW14_MARKER_TYPE int
#endif

#ifdef RW64_UNSIGNED_RECURSION_RETURN
static unsigned int rw14_recurse(
    int depth, RW14_MARKER_TYPE marker, int use_execv)
#else
static int rw14_recurse(
    int depth, RW14_MARKER_TYPE marker, int use_execv)
#endif
{
#ifdef RW64_VOLATILE_LOCAL_CHECK
    volatile int local_check;
#else
    int local_check;
#endif
#ifdef RW64_VOLATILE_RESULT
    volatile int result;
#else
    int result;
#endif
#ifdef RW64_VLA_GUARD
    int scratch[depth ? 1 : 2];
#endif

#ifdef RW64_CFG_GUARD
    if (depth < -1000)
        return marker;
#endif
#ifdef RW64_VLA_GUARD
    scratch[0] = marker;
    if (scratch[0] != marker)
        return -999;
#endif

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
            RW14_BASE_REPORT("base result=%d\n", r);
            ++failures;
            return -999;
        }
        return marker;
    }

    local_check = marker + depth;
    result = rw14_recurse(depth - 1, marker, use_execv);
    if (result != marker) {
        RW14_DETAIL_REPORT(
            "result depth=%d marker=%d got=%d\n",
            depth, marker, result);
#ifdef RW14_DECREMENT_RESULT_FAILURE
        --failures;
#else
        ++failures;
#endif
        return -999;
    }
    if (local_check != marker + depth) {
        RW14_DETAIL_REPORT(
            "local depth=%d expected=%d got=%d\n",
            depth,
#ifdef RW14_REPEAT_MARKER_LOCAL_EXPECTED
            marker + marker,
#elif defined(RW14_SUBTRACT_LOCAL_EXPECTED)
            marker - depth,
#else
            marker + depth,
#endif
            local_check);
#ifdef RW14_DECREMENT_LOCAL_FAILURE
        --failures;
#else
        ++failures;
#endif
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
