/* Isolated runtime and structural controls for local function-pointer arrays. */

#include <stdio.h>

struct LocalDeclNode {
    int value;
};

typedef const struct LocalDeclNode *(*local_decl_fn)(
    const struct LocalDeclNode *node, int seed);

static struct LocalDeclNode local_decl_global = { 29 };

#ifdef LOCALDECLRET_RENAMED_SYMBOLS
#define LOCALDECLRET_SAME renamed_local_decl_same
#define LOCALDECLRET_GLOBAL renamed_local_decl_global
#define LOCALDECLRET_PICKERS renamed_local_decl_pickers
#else
#define LOCALDECLRET_SAME local_decl_same
#define LOCALDECLRET_GLOBAL local_decl_get_global
#define LOCALDECLRET_PICKERS pickers
#endif

#ifdef LOCALDECLRET_RENAMED
#define LOCALDECLRET_FUNCTION fixture_local_declaration_return_renamed
#else
#define LOCALDECLRET_FUNCTION fixture_local_declaration_return
#endif

#ifdef LOCALDECLRET_CHANGED_RETURN
#define LOCALDECLRET_RESULT 303
#else
#define LOCALDECLRET_RESULT 202
#endif

#ifdef LOCALDECLRET_VOLATILE_ARRAY
#define LOCALDECLRET_QUALIFIER volatile
#else
#define LOCALDECLRET_QUALIFIER
#endif

#ifdef LOCALDECLRET_ARRAY_THREE
#define LOCALDECLRET_ARRAY_LENGTH 3
#else
#define LOCALDECLRET_ARRAY_LENGTH 2
#endif

static const struct LocalDeclNode *LOCALDECLRET_SAME(
    const struct LocalDeclNode *node, int seed)
{
    (void) seed;
    return node;
}

static const struct LocalDeclNode *LOCALDECLRET_GLOBAL(
    const struct LocalDeclNode *node, int seed)
{
    (void) node;
    (void) seed;
    return &local_decl_global;
}

#ifdef LOCALDECLRET_EXTRA_CFG
volatile int local_decl_return_guard;
#endif

static int LOCALDECLRET_FUNCTION(void)
{
    LOCALDECLRET_QUALIFIER local_decl_fn
        LOCALDECLRET_PICKERS[LOCALDECLRET_ARRAY_LENGTH] = {
            LOCALDECLRET_SAME,
            LOCALDECLRET_GLOBAL
#ifdef LOCALDECLRET_ARRAY_THREE
            , LOCALDECLRET_SAME
#endif
        };

    (void) LOCALDECLRET_PICKERS;
#ifdef LOCALDECLRET_EXTRA_CFG
    if (local_decl_return_guard)
        return 7;
#endif
    return LOCALDECLRET_RESULT;
}

int main(void)
{
    struct LocalDeclNode local = { 11 };
    const struct LocalDeclNode *same =
        LOCALDECLRET_SAME(&local, 1);
    const struct LocalDeclNode *global =
        LOCALDECLRET_GLOBAL(&local, 2);
    int result = LOCALDECLRET_FUNCTION();
    int checksum = same->value + global->value;

    if (result != LOCALDECLRET_RESULT || checksum != 40) {
        printf(
            "local declaration return failures=1 result=%d checksum=%d\n",
            result, checksum);
        return 1;
    }
    printf(
        "local declaration return failures=0 result=%d checksum=%d\n",
        result, checksum);
    return 0;
}
