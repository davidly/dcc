/* Shapes that used to break dcc:
 *   - a switch on a long whose case values share their low 16 bits
 *     (65536L and 0) failed with an internal MIR error;
 *   - an operator chain of a few hundred terms took minutes to compile,
 *     because the value classifiers revisited each operand an exponential
 *     number of times. */
#include <stdio.h>

static int failures;

static void check(const char *what, long got, long want)
{
    if (got != want) {
        printf("%s: got %ld, want %ld\n", what, got, want);
        ++failures;
    }
}

static int wide_case(long v)
{
    switch (v) {
    case 65536L: return 1;
    case 0: return 2;
    case 131072L: return 3;
    case -65536L: return 4;
    default: return 5;
    }
}

static int unsigned_case(unsigned long v)
{
    switch (v) {
    case 0x10001UL: return 1;
    case 1: return 2;
    case 0xffff0001UL: return 3;
    }
    return 4;
}

static int int_chain(int x)
{
    return
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x + x + x + x + x + x + x + x + x +
        x + x + x + x;
}

static long long_chain(long y)
{
    return
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y +
        y + y + y + y + y + y + y + y + y + y + y + y;
}

static float float_chain(float z)
{
    return
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z + z + z + z + z +
        z + z + z + z + z + z + z + z;
}

static int ternary_chain(int x)
{
    return
        x == 0 ? 0 :
        x == 1 ? 3 :
        x == 2 ? 6 :
        x == 3 ? 9 :
        x == 4 ? 12 :
        x == 5 ? 15 :
        x == 6 ? 18 :
        x == 7 ? 21 :
        x == 8 ? 24 :
        x == 9 ? 27 :
        x == 10 ? 30 :
        x == 11 ? 33 :
        x == 12 ? 36 :
        x == 13 ? 39 :
        x == 14 ? 42 :
        x == 15 ? 45 :
        x == 16 ? 48 :
        x == 17 ? 51 :
        x == 18 ? 54 :
        x == 19 ? 57 :
        x == 20 ? 60 :
        x == 21 ? 63 :
        x == 22 ? 66 :
        x == 23 ? 69 :
        x == 24 ? 72 :
        x == 25 ? 75 :
        x == 26 ? 78 :
        x == 27 ? 81 :
        x == 28 ? 84 :
        x == 29 ? 87 :
        x == 30 ? 90 :
        x == 31 ? 93 :
        x == 32 ? 96 :
        x == 33 ? 99 :
        x == 34 ? 102 :
        x == 35 ? 105 :
        x == 36 ? 108 :
        x == 37 ? 111 :
        x == 38 ? 114 :
        x == 39 ? 117 :
        x == 40 ? 120 :
        x == 41 ? 123 :
        x == 42 ? 126 :
        x == 43 ? 129 :
        x == 44 ? 132 :
        x == 45 ? 135 :
        x == 46 ? 138 :
        x == 47 ? 141 :
        x == 48 ? 144 :
        x == 49 ? 147 :
        x == 50 ? 150 :
        x == 51 ? 153 :
        x == 52 ? 156 :
        x == 53 ? 159 :
        x == 54 ? 162 :
        x == 55 ? 165 :
        x == 56 ? 168 :
        x == 57 ? 171 :
        x == 58 ? 174 :
        x == 59 ? 177 :
        x == 60 ? 180 :
        x == 61 ? 183 :
        x == 62 ? 186 :
        x == 63 ? 189 :
        x == 64 ? 192 :
        x == 65 ? 195 :
        x == 66 ? 198 :
        x == 67 ? 201 :
        x == 68 ? 204 :
        x == 69 ? 207 :
        x == 70 ? 210 :
        x == 71 ? 213 :
        x == 72 ? 216 :
        x == 73 ? 219 :
        x == 74 ? 222 :
        x == 75 ? 225 :
        x == 76 ? 228 :
        x == 77 ? 231 :
        x == 78 ? 234 :
        x == 79 ? 237 :
        x == 80 ? 240 :
        x == 81 ? 243 :
        x == 82 ? 246 :
        x == 83 ? 249 :
        x == 84 ? 252 :
        x == 85 ? 255 :
        x == 86 ? 258 :
        x == 87 ? 261 :
        x == 88 ? 264 :
        x == 89 ? 267 :
        x == 90 ? 270 :
        x == 91 ? 273 :
        x == 92 ? 276 :
        x == 93 ? 279 :
        x == 94 ? 282 :
        x == 95 ? 285 :
        x == 96 ? 288 :
        x == 97 ? 291 :
        x == 98 ? 294 :
        x == 99 ? 297 :
        x == 100 ? 300 :
        x == 101 ? 303 :
        x == 102 ? 306 :
        x == 103 ? 309 :
        x == 104 ? 312 :
        x == 105 ? 315 :
        x == 106 ? 318 :
        x == 107 ? 321 :
        x == 108 ? 324 :
        x == 109 ? 327 :
        x == 110 ? 330 :
        x == 111 ? 333 :
        x == 112 ? 336 :
        x == 113 ? 339 :
        x == 114 ? 342 :
        x == 115 ? 345 :
        x == 116 ? 348 :
        x == 117 ? 351 :
        x == 118 ? 354 :
        x == 119 ? 357 :
        x == 120 ? 360 :
        x == 121 ? 363 :
        x == 122 ? 366 :
        x == 123 ? 369 :
        x == 124 ? 372 :
        x == 125 ? 375 :
        x == 126 ? 378 :
        x == 127 ? 381 :
        x == 128 ? 384 :
        x == 129 ? 387 :
        x == 130 ? 390 :
        x == 131 ? 393 :
        x == 132 ? 396 :
        x == 133 ? 399 :
        x == 134 ? 402 :
        x == 135 ? 405 :
        x == 136 ? 408 :
        x == 137 ? 411 :
        x == 138 ? 414 :
        x == 139 ? 417 :
        x == 140 ? 420 :
        x == 141 ? 423 :
        x == 142 ? 426 :
        x == 143 ? 429 :
        x == 144 ? 432 :
        x == 145 ? 435 :
        x == 146 ? 438 :
        x == 147 ? 441 :
        x == 148 ? 444 :
        x == 149 ? 447 :
        -1;
}

int main(void)
{
    check("65536L", wide_case(65536L), 1);
    check("0", wide_case(0), 2);
    check("131072L", wide_case(131072L), 3);
    check("-65536L", wide_case(-65536L), 4);
    check("1", wide_case(1), 5);
    check("0x10001", unsigned_case(0x10001UL), 1);
    check("1u", unsigned_case(1), 2);
    check("0xffff0001", unsigned_case(0xffff0001UL), 3);
    check("0x10000", unsigned_case(0x10000UL), 4);
    check("int chain", int_chain(3), 1200);
    check("long chain", long_chain(100000L), 30000000L);
    check("float chain", (long)float_chain(1.5f), 300);
    check("ternary chain", ternary_chain(149), 447);
    check("ternary miss", ternary_chain(150), -1);
    if (failures == 0)
        printf("tlongexp passed\n");
    return 0;
}
