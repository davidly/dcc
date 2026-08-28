/*
 * tfuncid.c - C99 __func__ predefined-identifier regression.
 *
 * The spelling must expand to the source-level function name independently
 * in every function body.  A name longer than M80's six significant
 * characters also verifies that dcc does not expose its assembly-name
 * shortening here.  Found via z88dk's Issue_493__func__.c.
 */
#include <stdio.h>
#include <string.h>

static int fails;

static void check(const char *name, const char *got, const char *expected)
{
    if (strcmp(got, expected) != 0) {
        printf("FAIL %s got %s expected %s\n", name, got, expected);
        ++fails;
    }
}

static const char *function_name_longer_than_six(void)
{
    const char *me = __func__;
    check("GNU alias", __FUNCTION__, "function_name_longer_than_six");
    return me;
}

static void accept_name(const char *callee)
{
    char *me = __func__;

    check("callee", callee, "main");
    check("local", me, "accept_name");
}

int main(void)
{
    fails = 0;
    accept_name(__func__);
    check("long source name", function_name_longer_than_six(),
          "function_name_longer_than_six");
    if (sizeof __func__ != 5) {
        printf("FAIL sizeof __func__ got %u expected 5\n",
               (unsigned int)sizeof __func__);
        ++fails;
    }
    if (sizeof __FUNCTION__ != 5) {
        printf("FAIL sizeof __FUNCTION__ got %u expected 5\n",
               (unsigned int)sizeof __FUNCTION__);
        ++fails;
    }

    if (fails) {
        printf("tfuncid failed: %d\n", fails);
        return 1;
    }
    printf("tfuncid completed with great success\n");
    return 0;
}
