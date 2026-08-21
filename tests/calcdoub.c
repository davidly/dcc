/**
 * @file calcdoub.c
 * @brief Decimal coefficient/exponent arithmetic for the CALC regression.
 *
 * @par Role
 * Implements normalized decimal parsing, formatting, comparison, arithmetic,
 * rounding, and integer powers using a signed CalcInt1024 coefficient and a
 * decimal exponent. CalcDouble is not the target's native floating type.
 *
 * @par Boundary
 * calc1024 supplies all coefficient arithmetic. This module does not parse
 * expressions or perform console I/O; calc.c owns those responsibilities.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calcdoub.h"

#define DOUBLE_DIGITS 17
#define DOUBLE_GUARD_DIGITS 18
#define DOUBLE_POWER_DIGITS 34
#define DOUBLE_MAX_SCI_EXP 308
#define DOUBLE_MIN_SCI_EXP -324

static void power_ten(int count, struct CalcInt1024 *result)
{
    struct CalcInt1024 ten;
    struct CalcInt1024 next;

    xfrom(result, 1);
    xfrom(&ten, 10);
    while (count-- > 0)
    {
        xmul(result, &ten, &next);
        *result = next;
    }
}

static int absolute_digits(const struct CalcInt1024 *value, char *buffer)
{
    int length;

    xfmt(value, buffer);
    if (buffer[0] == '-')
        memmove(buffer, buffer + 1, strlen(buffer));
    length = (int)strlen(buffer);
    return length;
}

static int normalize_precision(struct CalcDouble *value, int precision)
{
    struct CalcInt1024 absolute;
    struct CalcInt1024 divisor;
    struct CalcInt1024 quotient;
    struct CalcInt1024 remainder;
    struct CalcInt1024 doubled;
    struct CalcInt1024 one;
    struct CalcInt1024 ten;
    char digits[CALC_INT_DECIMAL_DIGITS];
    int negative;
    int length;
    int remove;
    int scientific_exponent;

    if (xiszero(&value->coefficient))
    {
        value->exponent = 0;
        return 0;
    }
    negative = xisneg(&value->coefficient);
    if (negative)
        xnegate(&value->coefficient, &absolute);
    else
        absolute = value->coefficient;

    length = absolute_digits(&absolute, digits);
    if (length > precision)
    {
        remove = length - precision;
        power_ten(remove, &divisor);
        xdiv(&absolute, &divisor, &quotient, &remainder);
        xadd(&remainder, &remainder, &doubled);
        if (xcomp(&doubled, &divisor) >= 0)
        {
            xfrom(&one, 1);
            xadd(&quotient, &one, &absolute);
        }
        else
        {
            absolute = quotient;
        }
        value->exponent += remove;
        length = absolute_digits(&absolute, digits);
        if (length > precision)
        {
            xfrom(&ten, 10);
            xdiv(&absolute, &ten, &absolute, &remainder);
            ++value->exponent;
            --length;
        }
    }

    xfrom(&ten, 10);
    while (!xiszero(&absolute))
    {
        xdiv(&absolute, &ten, &quotient, &remainder);
        if (!xiszero(&remainder))
            break;
        absolute = quotient;
        ++value->exponent;
        --length;
    }
    if (negative)
        xnegate(&absolute, &value->coefficient);
    else
        value->coefficient = absolute;

    scientific_exponent = value->exponent + length - 1;
    if (scientific_exponent > DOUBLE_MAX_SCI_EXP)
        return 1;
    if (scientific_exponent < DOUBLE_MIN_SCI_EXP)
        dzero(value);
    return 0;
}

static int normalize(struct CalcDouble *value)
{
    return normalize_precision(value, DOUBLE_DIGITS);
}

void dzero(struct CalcDouble *value)
{
    xzero(&value->coefficient);
    value->exponent = 0;
}

void dfromi(const struct CalcInt1024 *value, struct CalcDouble *result)
{
    result->coefficient = *value;
    result->exponent = 0;
    normalize(result);
}

int dparse(const char *text, struct CalcDouble *result)
{
    char significant[DOUBLE_GUARD_DIGITS + 1];
    char coefficient[DOUBLE_DIGITS + 1];
    int significant_count;
    int stored;
    int fractional_digits;
    int exponent;
    int exponent_sign;
    int saw_nonzero;
    int index;
    struct CalcInt1024 one;

    significant_count = 0;
    stored = 0;
    fractional_digits = 0;
    saw_nonzero = 0;
    while (*text >= '0' && *text <= '9')
    {
        if (*text != '0' || saw_nonzero)
        {
            saw_nonzero = 1;
            ++significant_count;
            if (stored < DOUBLE_GUARD_DIGITS)
                significant[stored++] = *text;
        }
        ++text;
    }
    if (*text == '.')
    {
        ++text;
        while (*text >= '0' && *text <= '9')
        {
            ++fractional_digits;
            if (*text != '0' || saw_nonzero)
            {
                saw_nonzero = 1;
                ++significant_count;
                if (stored < DOUBLE_GUARD_DIGITS)
                    significant[stored++] = *text;
            }
            ++text;
        }
    }
    exponent = 0;
    exponent_sign = 1;
    if (*text == 'e' || *text == 'E')
    {
        ++text;
        if (*text == '+' || *text == '-')
        {
            if (*text == '-')
                exponent_sign = -1;
            ++text;
        }
        if (*text < '0' || *text > '9')
            return 1;
        while (*text >= '0' && *text <= '9')
        {
            if (exponent < 10000)
                exponent = exponent * 10 + (*text - '0');
            ++text;
        }
    }
    if (*text != '\0')
        return 1;
    if (!saw_nonzero)
    {
        dzero(result);
        return 0;
    }

    for (index = 0; index < DOUBLE_DIGITS && index < stored; ++index)
        coefficient[index] = significant[index];
    coefficient[index] = '\0';
    if (xparse(coefficient, &result->coefficient))
        return 1;
    result->exponent = exponent_sign * exponent - fractional_digits;
    if (significant_count > DOUBLE_DIGITS)
        result->exponent += significant_count - DOUBLE_DIGITS;
    if (stored > DOUBLE_DIGITS && significant[DOUBLE_DIGITS] >= '5')
    {
        xfrom(&one, 1);
        xadd(&result->coefficient, &one, &result->coefficient);
    }
    return normalize(result);
}

int diszero(const struct CalcDouble *value)
{
    return xiszero(&value->coefficient);
}

int disneg(const struct CalcDouble *value)
{
    return xisneg(&value->coefficient);
}

int disint(const struct CalcDouble *value)
{
    struct CalcInt1024 divisor;
    struct CalcInt1024 quotient;
    struct CalcInt1024 remainder;

    if (value->exponent >= 0)
        return 1;
    if (-value->exponent > DOUBLE_DIGITS)
        return diszero(value);
    power_ten(-value->exponent, &divisor);
    xdiv(&value->coefficient, &divisor, &quotient, &remainder);
    return xiszero(&remainder);
}

void dneg(const struct CalcDouble *value, struct CalcDouble *result)
{
    *result = *value;
    xnegate(&value->coefficient, &result->coefficient);
}

static int magnitude_compare(const struct CalcDouble *left,
                             const struct CalcDouble *right)
{
    char left_digits[80];
    char right_digits[80];
    int left_length;
    int right_length;
    int left_scientific;
    int right_scientific;
    int index;
    char left_digit;
    char right_digit;

    left_length = absolute_digits(&left->coefficient, left_digits);
    right_length = absolute_digits(&right->coefficient, right_digits);
    left_scientific = left->exponent + left_length;
    right_scientific = right->exponent + right_length;
    if (left_scientific < right_scientific)
        return -1;
    if (left_scientific > right_scientific)
        return 1;
    for (index = 0; index < DOUBLE_DIGITS; ++index)
    {
        left_digit = index < left_length ? left_digits[index] : '0';
        right_digit = index < right_length ? right_digits[index] : '0';
        if (left_digit < right_digit)
            return -1;
        if (left_digit > right_digit)
            return 1;
    }
    return 0;
}

int dcomp(const struct CalcDouble *left, const struct CalcDouble *right)
{
    int comparison;

    if (disneg(left) != disneg(right))
        return disneg(left) ? -1 : 1;
    comparison = magnitude_compare(left, right);
    return disneg(left) ? -comparison : comparison;
}

int dadd(const struct CalcDouble *left, const struct CalcDouble *right,
         struct CalcDouble *result)
{
    const struct CalcDouble *higher;
    const struct CalcDouble *lower;
    struct CalcInt1024 factor;
    struct CalcInt1024 scaled;
    int difference;

    if (diszero(left))
    {
        *result = *right;
        return 0;
    }
    if (diszero(right))
    {
        *result = *left;
        return 0;
    }
    if (left->exponent >= right->exponent)
    {
        higher = left;
        lower = right;
    }
    else
    {
        higher = right;
        lower = left;
    }
    difference = higher->exponent - lower->exponent;
    if (difference > DOUBLE_DIGITS + 1)
    {
        *result = *higher;
        return 0;
    }
    power_ten(difference, &factor);
    if (xmul(&higher->coefficient, &factor, &scaled))
        return 1;
    if (xadd(&scaled, &lower->coefficient, &result->coefficient))
        return 1;
    result->exponent = lower->exponent;
    return normalize(result);
}

int dsub(const struct CalcDouble *left, const struct CalcDouble *right,
         struct CalcDouble *result)
{
    struct CalcDouble negative;

    dneg(right, &negative);
    return dadd(left, &negative, result);
}

int dmul(const struct CalcDouble *left, const struct CalcDouble *right,
         struct CalcDouble *result)
{
    if (xmul(&left->coefficient, &right->coefficient,
             &result->coefficient))
        return 1;
    result->exponent = left->exponent + right->exponent;
    return normalize(result);
}

static int dmul_precision(const struct CalcDouble *left,
                          const struct CalcDouble *right,
                          struct CalcDouble *result, int precision)
{
    if (xmul(&left->coefficient, &right->coefficient,
             &result->coefficient))
        return 1;
    result->exponent = left->exponent + right->exponent;
    return normalize_precision(result, precision);
}

static int ddiv_precision(const struct CalcDouble *left,
                          const struct CalcDouble *right,
                          struct CalcDouble *result, int precision)
{
    struct CalcInt1024 factor;
    struct CalcInt1024 numerator;
    struct CalcInt1024 remainder;
    char left_digits[80];
    char right_digits[80];
    int scale;

    if (diszero(right))
        return 2;
    scale = precision + 1 +
            absolute_digits(&right->coefficient, right_digits) -
            absolute_digits(&left->coefficient, left_digits);
    power_ten(scale, &factor);
    if (xmul(&left->coefficient, &factor, &numerator))
        return 1;
    if (xdiv(&numerator, &right->coefficient,
             &result->coefficient, &remainder) != 0)
        return 1;
    result->exponent = left->exponent - right->exponent -
                       scale;
    return normalize_precision(result, precision);
}

int ddiv(const struct CalcDouble *left, const struct CalcDouble *right,
         struct CalcDouble *result)
{
    return ddiv_precision(left, right, result, DOUBLE_DIGITS);
}

static int exponent_value(const struct CalcDouble *value, int *exponent)
{
    char buffer[80];
    int length;

    if (!disint(value))
        return 0;
    dfmt(value, buffer);
    length = (int)strlen(buffer);
    if (length > 6)
        return 0;
    *exponent = atoi(buffer);
    return 1;
}

int dpow(const struct CalcDouble *base, const struct CalcDouble *exponent,
         struct CalcDouble *result)
{
    struct CalcDouble product;
    struct CalcDouble factor;
    struct CalcDouble one;
    struct CalcDouble next;
    struct CalcInt1024 integer_one;
    int power;
    int negative;

    if (!exponent_value(exponent, &power))
        return 3;
    negative = power < 0;
    if (negative)
        power = -power;
    xfrom(&integer_one, 1);
    dfromi(&integer_one, &one);
    product = one;
    factor = *base;
    while (power > 0)
    {
        if ((power & 1) != 0)
        {
            if (dmul_precision(&product, &factor, &next,
                               DOUBLE_POWER_DIGITS))
                return 1;
            product = next;
        }
        power >>= 1;
        if (power != 0)
        {
            if (dmul_precision(&factor, &factor, &next,
                               DOUBLE_POWER_DIGITS))
                return 1;
            factor = next;
        }
    }
    if (negative)
    {
        if (diszero(&product))
            return 2;
        if (ddiv_precision(&one, &product, result,
                           DOUBLE_POWER_DIGITS))
            return 1;
        if (normalize(result))
            return 1;
    }
    else
    {
        if (normalize(&product))
            return 1;
        *result = product;
    }
    return 0;
}

void dfmt(const struct CalcDouble *value, char *buffer)
{
    char digits[80];
    int length;
    int point;
    int scientific;
    int index;
    char *output;

    if (diszero(value))
    {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    output = buffer;
    if (disneg(value))
        *output++ = '-';
    length = absolute_digits(&value->coefficient, digits);
    point = length + value->exponent;
    scientific = point - 1;
    if (point > 0 && point <= DOUBLE_DIGITS)
    {
        for (index = 0; index < length; ++index)
        {
            if (index == point)
                *output++ = '.';
            *output++ = digits[index];
        }
        while (index++ < point)
            *output++ = '0';
    }
    else if (point <= 0 && point >= -6)
    {
        *output++ = '0';
        *output++ = '.';
        for (index = 0; index < -point; ++index)
            *output++ = '0';
        strcpy(output, digits);
        output += strlen(digits);
    }
    else
    {
        *output++ = digits[0];
        if (length > 1)
        {
            *output++ = '.';
            for (index = 1; index < length; ++index)
                *output++ = digits[index];
        }
        *output++ = 'e';
        sprintf(output, "%d", scientific);
        output += strlen(output);
    }
    *output = '\0';
}