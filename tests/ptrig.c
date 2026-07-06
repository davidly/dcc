/*
    this test implements various floating point functions and either prints or validates the results
    so regressions can be found.
*/

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <float.h>  /* For FLT_EPSILON */
#include <stdlib.h>  /* For exit */

#define _countof( X ) ( sizeof( X ) / sizeof( X[0] ) )
const size_t max_N_Iterations = 12; // 12 will fit in uint32_t

#define TRIG_FLT_EPSILON 0.00004f /* much larger than FLT_EPSILON because Taylor Series version is not great */

static void check_same_f( const char * operation, float a, float b, float dbgval )
{
    float diff = a - b;
    float abs_diff = fabsf( diff );
    //printf( "abs_diff %f, TRUG_FLT_EPSILON %f\n", abs_diff, TRIG_FLT_EPSILON );
    bool eq = ( abs_diff <= TRIG_FLT_EPSILON );
    if ( !eq )
    {
        printf( "operation %s: float %.20f is not the same as float %.20f\n", operation, a, b );
        printf( "  original value: %.20f\n", dbgval );
        exit( 0 );
    }
} //check_same_f

static uint32_t factorial( uint32_t n )
{
    if ( 0 == n )
        return 1;
    return n * factorial( n - 1 );
}

#define LN2 0.69314718056f

float xexpf(float x)
{
    float sum, term, r;
    int n, i;

    /* Handle extreme edge cases to prevent overflow */
    if (x > 88.0f) {
        return 3.40282347e+38f; /* Approximate FLT_MAX */
    }
    if (x < -88.0f) {
        return 0.0f;
    }

    /* Argument reduction: x = n * ln(2) + r */
    /* C89 requires explicit floor/rounding logic if math.h is omitted */
    n = (int)(x / LN2);
    if (x < 0.0f && (x / LN2) != (float)n) {
        n--;
    }
    r = x - (float)n * LN2;

    /* Taylor Series for e^r = 1 + r + r^2/2! + r^3/3! + ... */
    sum = 1.0f;
    term = 1.0f;
    for (i = 1; i <= 10; i++) {
        term = term * r / (float)i;
        sum += term;
    }

    /* Scale back up by 2^n using bit shifting or multiplication loops */
    if (n >= 0) {
        while (n > 0) {
            sum *= 2.0f;
            n--;
        }
    } else {
        while (n < 0) {
            sum /= 2.0f;
            n++;
        }
    }

    return sum;
} //expf

float xlogf(float x)
{
    float m, y, y2, term, sum;
    int n, i;

    /* Domain validation: log(x) is undefined for x <= 0 */
    if (x <= 0.0f) {
        return -3.40282347e+38f; /* Return large negative value or NaN proxy */
    }

    /* Argument reduction: Find n such that x = m * 2^n and 1.0 <= m < 2.0 */
    n = 0;
    m = x;
    if (m >= 1.0f) {
        while (m >= 2.0f) {
            m /= 2.0f;
            n++;
        }
    } else {
        while (m < 1.0f) {
            m *= 2.0f;
            n--;
        }
    }

    /* Map m to variable y where y = (m - 1) / (m + 1) */
    y = (m - 1.0f) / (m + 1.0f);
    y2 = y * y;

    /* Taylor Series for ln(m) = 2 * (y + y^3/3 + y^5/5 + ...) */
    sum = 0.0f;
    term = y;
    for (i = 1; i <= 13; i += 2) {
        sum += term / (float)i;
        term *= y2;
    }
    sum *= 2.0f;

    /* Reconstruct: ln(x) = ln(m) + n * ln(2) */
    return sum + (float)n * LN2;
} //logf

float xpowf(float base, float exponent)
{
    float result;
    long int_exp;

    /* Case 1: Exponent is exactly 0 -> x^0 = 1 */
    if (exponent == 0.0f) {
        return 1.0f;
    }

    /* Case 2: Base is exactly 0 -> 0^y */
    if (base == 0.0f) {
        if (exponent > 0.0f) {
            return 0.0f;
        } else {
            /* Division by zero / undefined case, return 0 or INF depending on platform handling */
            return 0.0f; 
        }
    }

    /* Case 3: Base is negative */
    if (base < 0.0f) {
        /* Check if the exponent is a whole integer */
        if (exponent == (float)((long)exponent)) {
            int_exp = (long)exponent;
            
            /* Calculate using the absolute value of the base */
            result = (float)expf(int_exp * logf(-base));
            
            /* If the exponent is odd, negate the result */
            if (int_exp % 2 != 0) {
                return -result;
            }
            return result;
        } else {
            /* Domain Error: Negative base with a fractional exponent results in a complex number */
            return 0.0f; 
        }
    }

    /* Case 4: General positive base fractional exponent (x^y = e^(y * ln(x))) */
    return (float)expf(exponent * logf(base));
} //powf

float sxsinf(float x) /* this version is much slower than the sinf() version. It's only here for testing */
{
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318531f;
    float result = 0.0f;
    int sign = 1;
    uint8_t i;

    x = fmodf(x, TWO_PI);
    if (x > PI)  x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    if (x > 1.57079632f) {
        x = PI - x;
    } else if (x < -1.57079632f) {
        x = -PI - x;
    }

    for (i = 1; i <= max_N_Iterations; i++) {
        result += sign * powf(x, 2 * i - 1) / factorial(2 * i - 1);
        sign *= -1;
    }

    return result;
}

/* results compare to msvc's results aside from the occasional least-significant digit */

/* Core loopless ATAN polynomial function used by both atanf and atan2f */
float xatanf(float x)
{
    float sign = 1.0f;
    float bias = 0.0f;

    if (x < 0.0f) {
        sign = -1.0f;
        x = -x;
    }

    if (x > 2.41421356f) {         
        bias = 1.57079632f;        
        x = -1.0f / x;
    } else if (x > 0.41421356f) {  
        bias = 0.78539816f;        
        x = (x - 1.0f) / (x + 1.0f);
    }

    float x2 = x * x;
    float poly = x + x * x2 * (-0.33333145f + x2 * (0.19993550f + x2 * (-0.14208899f + x2 * 0.10656263f)));

    return sign * (bias + poly);
}

float xcosf(float x)
{
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318531f;
    float sign = 1.0f;
    
    x = fmodf(x, TWO_PI);
    if (x > PI)  x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    if (x > 1.57079632f) {
        x = PI - x;
        sign = -1.0f;
    } else if (x < -1.57079632f) {
        x = -PI - x;
        sign = -1.0f;
    }

    float x2 = x * x;
    return sign * ( 1.0f + x2 * (-0.49999286f + x2 * (0.04163351f + x2 * (-0.00136122f))) );
}

float xsinf(float x)
{
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318531f;
    float sign = 1.0f;

    /* 1. Argument reduction to [-PI, PI] */
    x = fmodf(x, TWO_PI);
    if (x > PI)  x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    /* 2. Map quadrants to [-PI/2, PI/2] */
    if (x > 1.57079632f) {
        x = PI - x;
    } else if (x < -1.57079632f) {
        x = -PI - x;
    }

    /* 3. Evaluate 4-term Sinf Minimax Polynomial via Horner's scheme */
    float x2 = x * x;
    return x + x * x2 * (-0.16665724f + x2 * (0.00831348f + x2 * (-0.00018521f)));
}

float xtanf(float x)
{
    const float PI = 3.14159265f;
    const float HALF_PI = 1.57079632f;
    const float QUARTER_PI = 0.78539816f;
    int invert = 0;

    float x_reduced = fmodf(x, PI);
    if (x_reduced > HALF_PI) {
        x_reduced -= PI;
    } else if (x_reduced < -HALF_PI) {
        x_reduced += PI;
    }

    if (x_reduced > QUARTER_PI) {
        x_reduced = HALF_PI - x_reduced;
        invert = 1;
    } else if (x_reduced < -QUARTER_PI) {
        x_reduced = -HALF_PI - x_reduced;
        invert = 1;
    }

    float x2 = x_reduced * x_reduced;
    float num = x_reduced * (15.0f - x2);
    float den = 15.0f - 6.0f * x2;

    if (den == 0.0f) return 0.0f;
    float result = num / den;

    if (invert) {
        if (result == 0.0f) return 0.0f;
        result = 1.0f / result;
    }

    return result;
}

float xatn2f(float y, float x) 
{
    const float PI = 3.14159265f;
    const float HALF_PI = 1.57079632f;

    if (x == 0.0f) {
        if (y > 0.0f) return  HALF_PI; 
        if (y < 0.0f) return -HALF_PI; 
        return 0.0f;                   
    }

    float ratio = y / x;

    if (x > 0.0f) {
        return atanf(ratio);
    } else {
        if (y >= 0.0f) {
            return atanf(ratio) + PI; 
        } else {
            return atanf(ratio) - PI; 
        }
    }
}

float xasinf(float x) 
{
    const float HALF_PI = 1.57079632f;
    float sign = 1.0f;

    if (x < 0.0f) {
        sign = -1.0f;
        x = -x;
    }

    if (x > 1.0f) return 0.0f;

    if (x > 0.5f) {
        float transform = (float)sqrtf(0.5f * (1.0f - x));
        float sub_asin = asinf(transform);
        return sign * (HALF_PI - 2.0f * sub_asin);
    }

    float x2 = x * x;
    float poly = x + x * x2 * (0.16666752f + x2 * (0.07495300f + x2 * (0.04547002f + x2 * 0.02417036f)));

    return sign * poly;
}

float xacosf(float x) 
{
    const float HALF_PI = 1.57079632f;
    return HALF_PI - asinf(x);
}

int main(void) 
{
    float angles[11];
    float inverse_inputs[9];
    float pairs_y[9];
    float pairs_x[9];
    int i;

    angles[0] = 0.0f;   angles[1] = 0.5f;   angles[2] = 1.0f; 
    angles[3] = 1.5f;   angles[4] = -0.5f;  angles[5] = -1.0f; 
    angles[6] = -1.5f;  angles[7] = 3.0f;   angles[8] = -3.0f; 
    angles[9] = 5.0f;   angles[10] = -5.0f;

    inverse_inputs[0] = 0.0f;   inverse_inputs[1] = 0.25f;  inverse_inputs[2] = 0.5f;
    inverse_inputs[3] = 0.75f;  inverse_inputs[4] = 1.0f;   inverse_inputs[5] = -0.25f;
    inverse_inputs[6] = -0.5f;  inverse_inputs[7] = -0.75f; inverse_inputs[8] = -1.0f;

    pairs_y[0] = 0.0f;  pairs_x[0] = 1.0f;
    pairs_y[1] = 1.0f;  pairs_x[1] = 1.0f;
    pairs_y[2] = 1.0f;  pairs_x[2] = 0.0f;
    pairs_y[3] = 1.0f;  pairs_x[3] = -1.0f;
    pairs_y[4] = 0.0f;  pairs_x[4] = -1.0f;
    pairs_y[5] = -1.0f; pairs_x[5] = -1.0f;
    pairs_y[6] = -1.0f; pairs_x[6] = 0.0f;
    pairs_y[7] = -1.0f; pairs_x[7] = 1.0f;
    pairs_y[8] = 0.0f;  pairs_x[8] = 0.0f;

    /* 1. Direct Sweep Testing: sinf, cosf, tanf, atanf */
    for (i = 0; i < _countof( angles ); i++) {
        float test_val;
        test_val = angles[i];
        printf( "sin  %f: %f\n", test_val, xsinf( test_val ) );
        printf( "cos  %f: %f\n", test_val, xcosf( test_val ) );
        printf( "tan  %f: %f\n", test_val, xtanf( test_val ) );
        printf( "atan %f: %f\n", test_val, xatanf( test_val ) );
    }

    /* 2. Inverse Bound Sweep Testing: asinf, acosf */
    for (i = 0; i < _countof( inverse_inputs ); i++) {
        float test_val;
        test_val = inverse_inputs[i];
        printf( "asin %f: %f\n", test_val, xasinf( test_val ) );
        printf( "acos %f: %f\n", test_val, xacosf( test_val ) );
    }

    /* 3. Coordinate System Testing: atan2f */
    for (i = 0; i < _countof( pairs_y ); i++) {
        float y, x;
        y = pairs_y[i];
        x = pairs_x[i];
        printf( "atan2f %f %f: %f\n", y, x, xatn2f( y, x ) );
    }

     /* 4. Direct Sweep Testing: sinf vs. xsinf (Taylor Series Slow) version */
    for (i = 0; i < _countof( angles ); i++) {
        float test_val = angles[i];
        float result = xsinf( test_val );
        float xresult = sxsinf( test_val );
        printf( "value %f. sinf %f, xsinf %f\n", test_val, result, xresult );
        check_same_f( "sinf vs. xsinf", result, xresult, test_val );
    }

   printf( "ttrig completed with great success\n" );
}
