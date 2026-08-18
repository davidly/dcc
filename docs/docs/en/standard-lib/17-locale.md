# Localization (`locale.h`)

Include [`locale.h`](17-locale.md) for locale categories and fixed-C-locale
formatting conventions.

## Types and Macros

<!-- LOCALE-SYMBOL-TABLE: all -->

`struct lconv` has the standard C89 numeric and monetary formatting members.
In DCC's fixed `C` locale, `decimal_point` is `"."`, every other string is
empty, and every `char` field is `CHAR_MAX` (not applicable).

## Functions

<!-- LOCALE-FUNCTION-TABLE: all -->

## Fixed locale model

The active locale is always `"C"`. `setlocale(category, NULL)` queries it, and
requesting either `"C"` or the implementation default `""` succeeds and
returns `"C"`. Any other locale name returns `NULL`. Because every category is
fixed, `category` does not change the result.

`localeconv()` returns the same static `struct lconv` object on every call.
