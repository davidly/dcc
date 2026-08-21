#pragma once

#include "calc1024.h"

struct CalcDouble
{
    struct CalcInt1024 coefficient;
    int exponent;
};

void dzero(struct CalcDouble *value);
void dfromi(const struct CalcInt1024 *value, struct CalcDouble *result);
int dparse(const char *text, struct CalcDouble *result);
int diszero(const struct CalcDouble *value);
int disneg(const struct CalcDouble *value);
int disint(const struct CalcDouble *value);
void dneg(const struct CalcDouble *value, struct CalcDouble *result);
int dcomp(const struct CalcDouble *left, const struct CalcDouble *right);
int dadd(const struct CalcDouble *left, const struct CalcDouble *right,
         struct CalcDouble *result);
int dsub(const struct CalcDouble *left, const struct CalcDouble *right,
         struct CalcDouble *result);
int dmul(const struct CalcDouble *left, const struct CalcDouble *right,
         struct CalcDouble *result);
int ddiv(const struct CalcDouble *left, const struct CalcDouble *right,
         struct CalcDouble *result);
int dpow(const struct CalcDouble *base, const struct CalcDouble *exponent,
         struct CalcDouble *result);
void dfmt(const struct CalcDouble *value, char *buffer);
