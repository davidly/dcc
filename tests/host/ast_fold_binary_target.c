#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_ast_gen_internal.h"

static int failures;

static void expect_fold(const char *name, int strict, int op,
                        int left_type, long left_value,
                        int right_type, long right_value,
                        int expected_ok, long expected_value)
{
    struct AstNode left;
    struct AstNode right;
    struct AstNode binary;
    long value = 0x13579bdfL;
    int ok;

    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&binary, 0, sizeof(binary));
    left.kind = AST_INT_LIT;
    left.type = left_type;
    left.ival = left_value;
    right.kind = AST_INT_LIT;
    right.type = right_type;
    right.ival = right_value;
    binary.kind = AST_BINARY;
    binary.op = op;
    binary.a = &left;
    binary.b = &right;

    ok = strict ? ast_const_fold_strict(&binary, &value)
                : ast_const_scalar_fold(&binary, &value);
    if (ok != expected_ok ||
        (expected_ok ? value != expected_value : value != 0x13579bdfL)) {
        fprintf(stderr, "FAIL %s%s: ok=%d value=%ld\n",
                name, strict ? " strict" : " scalar", ok, value);
        ++failures;
    }
}

static void check_fold(const char *name, int op,
                       int left_type, long left_value,
                       int right_type, long right_value,
                       int expected_ok, long expected_value)
{
    expect_fold(name, 0, op, left_type, left_value,
                right_type, right_value, expected_ok, expected_value);
    expect_fold(name, 1, op, left_type, left_value,
                right_type, right_value, expected_ok, expected_value);
}

int main(void)
{
    int int_pointer = type_add_ptr(TYPE_INT);
    int struct_type = TYPE_STRUCT | (1 << STRUCT_SHIFT);

    check_fold("signed int arithmetic shift", TOK_SHR,
               TYPE_INT, -32768L, TYPE_INT, 15L, 1, -1L);
    check_fold("unsigned int logical shift", TOK_SHR,
               TYPE_INT | TYPE_UNSIGNED, 0x8000L, TYPE_INT, 15L, 1, 1L);
    check_fold("signed long arithmetic shift", TOK_SHR,
               TYPE_LONG, (-2147483647L - 1L), TYPE_INT, 31L, 1, -1L);
    check_fold("unsigned wrap", '+',
               TYPE_INT | TYPE_UNSIGNED, 65535L, TYPE_INT, 1L, 1, 0L);
    check_fold("usual arithmetic conversion", TOK_EQ,
               TYPE_INT, -1L, TYPE_INT | TYPE_UNSIGNED, 65535L, 1, 1L);

    check_fold("negative shift count", TOK_SHL,
               TYPE_INT, 1L, TYPE_INT, -1L, 0, 0L);
    check_fold("int-width shift count", TOK_SHL,
               TYPE_INT, 1L, TYPE_INT, 16L, 0, 0L);
    check_fold("long-width shift count", TOK_SHR,
               TYPE_LONG, 1L, TYPE_INT, 32L, 0, 0L);
    check_fold("division by zero", '/',
               TYPE_INT, 7L, TYPE_INT, 0L, 0, 0L);
    check_fold("signed division overflow", '/',
               TYPE_INT, -32768L, TYPE_INT, -1L, 0, 0L);

    check_fold("float left shift operand", TOK_SHL,
               TYPE_FLOAT, 1L, TYPE_INT, 1L, 0, 0L);
    check_fold("float right shift operand", TOK_SHL,
               TYPE_INT, 1L, TYPE_FLOAT, 1L, 0, 0L);
    check_fold("pointer arithmetic operand", '+',
               int_pointer, 1L, TYPE_INT, 1L, 0, 0L);
    check_fold("pointer shift count", TOK_SHL,
               TYPE_INT, 1L, int_pointer, 1L, 0, 0L);
    check_fold("void arithmetic operand", '+',
               TYPE_VOID, 1L, TYPE_INT, 1L, 0, 0L);
    check_fold("struct arithmetic operand", '+',
               struct_type, 1L, TYPE_INT, 1L, 0, 0L);

    if (failures != 0)
        return 1;
    puts("ast_fold_binary_target host controls passed");
    return 0;
}
