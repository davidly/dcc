#pragma once

#define CALC_INT_WORDS 32
#define CALC_INT_DECIMAL_DIGITS 309

struct CalcInt1024
{
    unsigned long word[CALC_INT_WORDS];
};

void xzero(struct CalcInt1024 *value);
void xfrom(struct CalcInt1024 *value, int source);
int xparse(const char *text, struct CalcInt1024 *value);
int xiszero(const struct CalcInt1024 *value);
int xisneg(const struct CalcInt1024 *value);
int xcomp(const struct CalcInt1024 *left, const struct CalcInt1024 *right);
int xnegate(const struct CalcInt1024 *value, struct CalcInt1024 *result);
void xnot(const struct CalcInt1024 *value, struct CalcInt1024 *result);
int xadd(const struct CalcInt1024 *left, const struct CalcInt1024 *right,
         struct CalcInt1024 *result);
int xsub(const struct CalcInt1024 *left, const struct CalcInt1024 *right,
         struct CalcInt1024 *result);
int xmul(const struct CalcInt1024 *left, const struct CalcInt1024 *right,
         struct CalcInt1024 *result);
int xdiv(const struct CalcInt1024 *left, const struct CalcInt1024 *right,
         struct CalcInt1024 *quotient, struct CalcInt1024 *remainder);
void xfmt(const struct CalcInt1024 *value, char *buffer);
