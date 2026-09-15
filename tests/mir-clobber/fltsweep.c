#include <math.h>
#include <stdio.h>

#define _countof(X) (sizeof(X) / sizeof(X[0]))

#ifdef FLOAT_SWEEP_RENAMED
#define FLOAT_SWEEP_FUNCTION renamed_float_sweep_fixture
#else
#define FLOAT_SWEEP_FUNCTION float_sweep_fixture
#endif

#ifdef FLOAT_SWEEP_VOLATILE
#define FLOAT_SWEEP_QUALIFIER volatile
#else
#define FLOAT_SWEEP_QUALIFIER
#endif

static int failures;
static int checks;

static float slow_sine(float value)
{
    const float pi = 3.14159265f;
    const float two_pi = 6.28318531f;
    float square;
    float term;
    float sum;
    int index;

    value = fmodf(value, two_pi);
    if (value > pi)
        value -= two_pi;
    if (value < -pi)
        value += two_pi;
    square = value * value;
    term = value;
    sum = value;
    for (index = 1; index <= 7; ++index) {
        term *= -square /
            (float)((2 * index) * (2 * index + 1));
        sum += term;
    }
    return sum;
}

static void check_same(
    const char *operation, float actual, float independent, float input)
{
    float difference = fabsf(actual - independent);

    (void)operation;
    (void)input;
    ++checks;
    if (difference > 0.00004f)
        ++failures;
}

#define xsinf sinf
#define xcosf cosf
#define xtanf tanf
#define xatanf atanf
#define xasinf asinf
#define xacosf acosf
#define xatn2f atan2f
#define sxsinf slow_sine
#define check_same_f check_same

int FLOAT_SWEEP_FUNCTION(void)
{
    FLOAT_SWEEP_QUALIFIER float angles[11];
    FLOAT_SWEEP_QUALIFIER float inverse_inputs[9];
    FLOAT_SWEEP_QUALIFIER float pairs_y[9];
    FLOAT_SWEEP_QUALIFIER float pairs_x[9];
    int i;

#ifdef FLOAT_SWEEP_EXTRA_CFG
    if (failures < 0)
        return 1;
#endif
    angles[0] = 0.0f;   angles[1] = 0.5f;   angles[2] = 1.0f;
    angles[3] = 1.5f;   angles[4] = -0.5f;  angles[5] = -1.0f;
    angles[6] = -1.5f;  angles[7] = 3.0f;   angles[8] = -3.0f;
    angles[9] = 5.0f;   angles[10] = -5.0f;

    inverse_inputs[0] = 0.0f;   inverse_inputs[1] = 0.25f;
    inverse_inputs[2] = 0.5f;   inverse_inputs[3] = 0.75f;
    inverse_inputs[4] = 1.0f;   inverse_inputs[5] = -0.25f;
    inverse_inputs[6] = -0.5f;  inverse_inputs[7] = -0.75f;
    inverse_inputs[8] = -1.0f;

    pairs_y[0] = 0.0f;  pairs_x[0] = 1.0f;
    pairs_y[1] = 1.0f;  pairs_x[1] = 1.0f;
    pairs_y[2] = 1.0f;  pairs_x[2] = 0.0f;
    pairs_y[3] = 1.0f;  pairs_x[3] = -1.0f;
    pairs_y[4] = 0.0f;  pairs_x[4] = -1.0f;
    pairs_y[5] = -1.0f; pairs_x[5] = -1.0f;
    pairs_y[6] = -1.0f; pairs_x[6] = 0.0f;
    pairs_y[7] = -1.0f; pairs_x[7] = 1.0f;
    pairs_y[8] = 0.0f;  pairs_x[8] = 0.0f;

    for (i = 0; i < _countof( angles ); i++) {
        float test_val;

        test_val = angles[i];
        printf( "sin  %f: %f\n", test_val, xsinf( test_val ) );
        printf( "cos  %f: %f\n", test_val, xcosf( test_val ) );
        printf( "tan  %f: %f\n", test_val, xtanf( test_val ) );
        printf( "atan %f: %f\n", test_val, xatanf( test_val ) );
    }
    for (i = 0; i < _countof( inverse_inputs ); i++) {
        float test_val;

        test_val = inverse_inputs[i];
        printf( "asin %f: %f\n", test_val, xasinf( test_val ) );
        printf( "acos %f: %f\n", test_val, xacosf( test_val ) );
    }
    for (i = 0; i < _countof( pairs_y ); i++) {
        float y, x;

        y = pairs_y[i];
        x = pairs_x[i];
        printf( "atan2f %f %f: %f\n", y, x, xatn2f( y, x ) );
    }
    for (i = 0; i < _countof( angles ); i++) {
        float test_val = angles[i];
        float result = sinf(test_val);
        float xresult = sxsinf( test_val );

        printf( "value %f. sinf %f, xsinf %f\n",
            test_val, result, xresult );
        check_same_f(
            "sinf vs. xsinf", result, xresult, test_val );
    }
    printf( "float sweep completed\n" );
    return 0;
}

int main(void)
{
    FLOAT_SWEEP_FUNCTION();
    printf("float sweep failures=%d checks=%d\n", failures, checks);
    return failures != 0 || checks != 11;
}
