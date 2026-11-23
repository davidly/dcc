/* Abstract parameter declarators and adjusted arrays of function pointers. */
#include <stdio.h>

typedef int (*Unary)(int);
typedef Unary UnaryArray[4];
typedef long (*Wide)(long);
typedef Wide WideArray[2];
typedef float (*Floating)(float);
typedef Floating FloatArray[2];

static int apply(int (*[4])(int), int);
static int nested(int (int()), Unary);
static int grouped(int ([4]), int);
static long apply_wide(long (*[2])(long), int);
static float apply_float(float (*[2])(float), int);

static int add_one(int value) { return value + 1; }
static int double_value(int value) { return value * 2; }
static long wide_value(long value) { return value + 70000L; }
static float float_value(float value) { return value + 0.5f; }

static int apply(UnaryArray functions, int index)
{
    return (*functions[index])(index);
}

static int call_unary(Unary function)
{
    return function(7);
}

static int nested(int function(int callback()), Unary callback)
{
    return function(callback);
}

static int grouped(int values[4], int index)
{
    return values[index];
}

static long apply_wide(WideArray functions, int index)
{
    return (*functions[index])(3);
}

static float apply_float(FloatArray functions, int index)
{
    return (*functions[index])(3);
}

static int named(int (*functions[4])(int), int index)
{
    return functions[index](index);
}

int main(void)
{
    UnaryArray functions;
    WideArray wide_functions;
    FloatArray float_functions;
    int values[4] = { 2, 4, 6, 8 };
    functions[0] = add_one;
    functions[1] = double_value;
    functions[2] = add_one;
    functions[3] = double_value;
    wide_functions[0] = wide_value;
    wide_functions[1] = wide_value;
    float_functions[0] = float_value;
    float_functions[1] = float_value;
    printf("array callbacks %d %d %d\n",
           apply(functions, 0), apply(functions, 3), named(functions, 2));
    printf("nested callback %d\n", nested(call_unary, add_one));
    printf("grouped array %d\n", grouped(values, 3));
    printf("wide callbacks %ld %d\n", apply_wide(wide_functions, 1),
           apply_float(float_functions, 1) == 3.5f);
    return 0;
}
