/**
 * @file calc1024.c
 * @brief Signed 1024-bit integer implementation for the CALC regression.
 *
 * @par Role
 * This module implements parsing, formatting, comparison, arithmetic, and
 * bitwise operations over fixed little-endian arrays of 32-bit limbs. The
 * portable C path deliberately exercises reusable wide-integer compiler
 * patterns; Z80_ASM_OPTS selects equivalent hand-written kernels for the
 * comparison build.
 *
 * @par Boundary
 * The public integer contract is declared in calc1024.h. This layer has no
 * parser or console responsibilities, and calcdoub builds decimal arithmetic
 * on top of it.
 */
#include "calc1024.h"

#define SIGN_BIT 0x80000000UL
#define WORD_MAX 0xffffffffUL

#ifdef Z80_ASM_OPTS
#define unsigned_compare xacmp
extern int xacmp(const struct CalcInt1024 *left,
                 const struct CalcInt1024 *right);
#else
static int unsigned_compare(const struct CalcInt1024 *left,
                            const struct CalcInt1024 *right)
{
    int index;

    for (index = CALC_INT_WORDS - 1; index >= 0; --index)
    {
        if (left->word[index] < right->word[index])
            return -1;
        if (left->word[index] > right->word[index])
            return 1;
    }
    return 0;
}
#endif

#ifdef Z80_ASM_OPTS
#define unsigned_add xaadd
extern void xaadd(const struct CalcInt1024 *left,
                  const struct CalcInt1024 *right,
                  struct CalcInt1024 *result);
#else
static void unsigned_add(const struct CalcInt1024 *left,
                         const struct CalcInt1024 *right,
                         struct CalcInt1024 *result)
{
    unsigned long left_word;
    unsigned long sum;
    unsigned long with_carry;
    int carry;
    int next_carry;
    int index;

    carry = 0;
    for (index = 0; index < CALC_INT_WORDS; ++index)
    {
        left_word = left->word[index];
        sum = left_word + right->word[index];
        next_carry = sum < left_word;
        with_carry = sum + (unsigned long)carry;
        if (carry && with_carry == 0UL)
            next_carry = 1;
        result->word[index] = with_carry;
        carry = next_carry;
    }
}
#endif

#ifdef Z80_ASM_OPTS
#define unsigned_subtract xasub
extern void xasub(const struct CalcInt1024 *left,
                  const struct CalcInt1024 *right,
                  struct CalcInt1024 *result);
#else
static void unsigned_subtract(const struct CalcInt1024 *left,
                              const struct CalcInt1024 *right,
                              struct CalcInt1024 *result)
{
    unsigned long left_word;
    unsigned long difference;
    unsigned long with_borrow;
    int borrow;
    int next_borrow;
    int index;

    borrow = 0;
    for (index = 0; index < CALC_INT_WORDS; ++index)
    {
        left_word = left->word[index];
        difference = left_word - right->word[index];
        next_borrow = left_word < right->word[index];
        with_borrow = difference - (unsigned long)borrow;
        if (borrow && difference == 0UL)
            next_borrow = 1;
        result->word[index] = with_borrow;
        borrow = next_borrow;
    }
}
#endif

#ifdef Z80_ASM_OPTS
#define unsigned_negate xaneg
extern void xaneg(const struct CalcInt1024 *value,
                  struct CalcInt1024 *result);
#else
static void unsigned_negate(const struct CalcInt1024 *value,
                            struct CalcInt1024 *result)
{
    unsigned long word;
    int carry;
    int index;

    carry = 1;
    for (index = 0; index < CALC_INT_WORDS; ++index)
    {
        word = ~value->word[index] + (unsigned long)carry;
        carry = carry && word == 0UL;
        result->word[index] = word;
    }
}
#endif

static void magnitude(const struct CalcInt1024 *value,
                      struct CalcInt1024 *result)
{
    if (xisneg(value))
        unsigned_negate(value, result);
    else
        *result = *value;
}

#ifdef Z80_ASM_OPTS
#define shift_left xashl
extern void xashl(struct CalcInt1024 *value);
#else
static void shift_left(struct CalcInt1024 *value)
{
    unsigned long carry;
    unsigned long next_carry;
    int index;

    carry = 0UL;
    for (index = 0; index < CALC_INT_WORDS; ++index)
    {
        next_carry = value->word[index] >> 31;
        value->word[index] = (value->word[index] << 1) | carry;
        carry = next_carry;
    }
}
#endif

#ifdef Z80_ASM_OPTS
#define shift_right xashr
extern void xashr(struct CalcInt1024 *value);
#else
static void shift_right(struct CalcInt1024 *value)
{
    unsigned long carry;
    unsigned long next_carry;
    int index;

    carry = 0UL;
    for (index = CALC_INT_WORDS - 1; index >= 0; --index)
    {
        next_carry = value->word[index] & 1UL;
        value->word[index] = (value->word[index] >> 1) | (carry << 31);
        carry = next_carry;
    }
}
#endif

#ifdef Z80_ASM_OPTS
extern int xacmn(const struct CalcInt1024 *left,
                 const struct CalcInt1024 *right, int byte_count);
extern void xasbn(const struct CalcInt1024 *left,
                  const struct CalcInt1024 *right,
                  struct CalcInt1024 *result, int byte_count);
extern void xashn(struct CalcInt1024 *value, int byte_count);
#endif

#ifdef Z80_ASM_OPTS
    #asm
        ; Register-optimized 1024-bit unsigned kernels. CalcInt1024 is 128
        ; little-endian bytes. All routines preserve the caller's IX.

        public _xacmp
    _xacmp:
        push ix
        ld ix,0
        add ix,sp
        ld e,(ix+4)
        ld d,(ix+5)             ; DE = left
        ld l,(ix+6)
        ld h,(ix+7)             ; HL = right
        ld bc,127
        ex de,hl
        add hl,bc               ; HL = left + 127
        ex de,hl
        add hl,bc               ; DE = left + 127, HL = right + 127
        ld b,128
    _xacm1:
        ld a,(de)
        cp (hl)
        jr c,_xacml
        jr nz,_xacmg
        dec de
        dec hl
        djnz _xacm1
        ld hl,0
        pop ix
        ret
    _xacml:
        ld hl,-1
        pop ix
        ret
    _xacmg:
        ld hl,1
        pop ix
        ret

        public _xaadd
    _xaadd:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)             ; HL = left
        ld e,(ix+6)
        ld d,(ix+7)             ; DE = right
        ld c,(ix+8)
        ld b,(ix+9)
        push bc
        pop ix                  ; IX = result
        ld b,128
        or a                    ; clear carry
    _xaalp:
        ld a,(de)
        adc a,(hl)
        ld (ix+0),a
        inc de
        inc hl
        inc ix
        djnz _xaalp
        pop ix
        ret

        public _xasub
    _xasub:
        push ix
        ld ix,0
        add ix,sp
        ld e,(ix+4)
        ld d,(ix+5)             ; DE = left
        ld l,(ix+6)
        ld h,(ix+7)             ; HL = right
        ld c,(ix+8)
        ld b,(ix+9)
        push bc
        pop ix                  ; IX = result
        ld b,128
        or a                    ; clear borrow
    _xaslp:
        ld a,(de)
        sbc a,(hl)
        ld (ix+0),a
        inc de
        inc hl
        inc ix
        djnz _xaslp
        pop ix
        ret

        public _xaneg
    _xaneg:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)             ; HL = value
        ld e,(ix+6)
        ld d,(ix+7)             ; DE = result
        ld b,128
        or a                    ; initial borrow = 0
    _xanlp:
        ld a,0
        sbc a,(hl)              ; result = 0 - value
        ld (de),a
        inc hl
        inc de
        djnz _xanlp
        pop ix
        ret

        public _xashl
    _xashl:
        ld hl,2
        add hl,sp
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a                  ; HL = value
        ld b,128
        or a                    ; clear carry into low byte
    _xasll:
        rl (hl)
        inc hl
        djnz _xasll
        ret

        public _xashr
    _xashr:
        ld hl,2
        add hl,sp
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a                  ; HL = value
        ld de,127
        add hl,de               ; start at most-significant byte
        ld b,128
        or a                    ; clear carry into high byte
    _xasrl:
        rr (hl)
        dec hl
        djnz _xasrl
        ret

        public _xacmn
    _xacmn:
        push ix
        ld ix,0
        add ix,sp
        ld e,(ix+4)
        ld d,(ix+5)             ; DE = left
        ld l,(ix+6)
        ld h,(ix+7)             ; HL = right
        ld c,(ix+8)
        ld b,0
        dec bc
        ex de,hl
        add hl,bc
        ex de,hl
        add hl,bc
        ld b,(ix+8)
    _xacn1:
        ld a,(de)
        cp (hl)
        jr c,_xacnl
        jr nz,_xacng
        dec de
        dec hl
        djnz _xacn1
        ld hl,0
        pop ix
        ret
    _xacnl:
        ld hl,-1
        pop ix
        ret
    _xacng:
        ld hl,1
        pop ix
        ret

        public _xasbn
    _xasbn:
        push ix
        ld ix,0
        add ix,sp
        ld e,(ix+4)
        ld d,(ix+5)             ; DE = left
        ld l,(ix+6)
        ld h,(ix+7)             ; HL = right
        ld c,(ix+8)
        ld b,(ix+9)
        push bc
        ld b,(ix+10)            ; byte count
        or a
    _xasbl:
        ld a,(de)
        sbc a,(hl)
        ex (sp),hl
        ld (hl),a
        inc hl
        ex (sp),hl
        inc de
        inc hl
        djnz _xasbl
        pop bc
        pop ix
        ret

        public _xashn
    _xashn:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)
        ld b,(ix+6)
        or a
    _xasnl:
        rl (hl)
        inc hl
        djnz _xasnl
        pop ix
        ret

        public _xad10
    _xad10:
        ld hl,2
        add hl,sp
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a                  ; HL = value
        ld de,127
        add hl,de               ; scan from most-significant byte
        ld b,128
    _xad1s:
        ld a,(hl)
        or a
        jr nz,_xad1f
        dec hl
        djnz _xad1s
        ld hl,0
        ret
    _xad1f:
        xor a                   ; A = remainder
    _xad1b:
        ld c,(hl)               ; C = source byte
        ld e,0                  ; E = quotient byte
        ld d,8
    _xad1i:
        sla c                   ; next source bit into carry
        rla                     ; remainder = remainder * 2 + bit
        cp 10
        jr c,_xad1n
        sub 10
        scf                     ; quotient bit = 1
        jr _xad1q
    _xad1n:
        or a                    ; quotient bit = 0
    _xad1q:
        rl e
        dec d
        jr nz,_xad1i
        ld (hl),e
        dec hl
        djnz _xad1b
        ld l,a
        ld h,0
        ret
    #endasm
#endif

    static int low_bit(const struct CalcInt1024 *value)
{
    return (value->word[0] & 1UL) != 0UL;
}

static int bit_value(const struct CalcInt1024 *value, int bit)
{
    return (int)((value->word[bit / 32] >> (bit % 32)) & 1UL);
}

static void set_bit(struct CalcInt1024 *value, int bit)
{
    value->word[bit / 32] |= 1UL << (bit % 32);
}

static void unsigned_divide(const struct CalcInt1024 *dividend,
                            const struct CalcInt1024 *divisor,
                            struct CalcInt1024 *quotient,
                            struct CalcInt1024 *remainder)
{
    int bit;
    int carry;
#ifdef Z80_ASM_OPTS
    int active_words;
    int byte_count;
#endif

    xzero(quotient);
    xzero(remainder);
#ifdef Z80_ASM_OPTS
    active_words = CALC_INT_WORDS;
    while (active_words > 1 && divisor->word[active_words - 1] == 0UL)
        --active_words;
    if (active_words < CALC_INT_WORDS)
        ++active_words;
    byte_count = active_words * 4;
#endif
    bit = (CALC_INT_WORDS * 32) - 1;
    while (bit >= 0 && bit_value(dividend, bit) == 0)
        --bit;
    while (bit >= 0)
    {
#ifdef Z80_ASM_OPTS
        carry = (remainder->word[active_words - 1] & SIGN_BIT) != 0UL;
        xashn(remainder, byte_count);
#else
        carry = (remainder->word[CALC_INT_WORDS - 1] & SIGN_BIT) != 0UL;
        shift_left(remainder);
#endif
        remainder->word[0] |= (unsigned long)bit_value(dividend, bit);
#ifdef Z80_ASM_OPTS
        if (carry || xacmn(remainder, divisor, byte_count) >= 0)
        {
            xasbn(remainder, divisor, remainder, byte_count);
#else
        if (carry || unsigned_compare(remainder, divisor) >= 0)
        {
            unsigned_subtract(remainder, divisor, remainder);
#endif
            set_bit(quotient, bit);
        }
        --bit;
    }
}

static int unsigned_multiply_limited(const struct CalcInt1024 *left,
                                     const struct CalcInt1024 *right,
                                     const struct CalcInt1024 *limit,
                                     struct CalcInt1024 *result)
{
    struct CalcInt1024 multiplicand;
    struct CalcInt1024 multiplier;
    struct CalcInt1024 room;

    multiplicand = *left;
    multiplier = *right;
    xzero(result);
    while (!xiszero(&multiplier))
    {
        if (low_bit(&multiplier))
        {
            unsigned_subtract(limit, result, &room);
            if (unsigned_compare(&multiplicand, &room) > 0)
                return 1;
            unsigned_add(result, &multiplicand, result);
        }
        shift_right(&multiplier);
        if (!xiszero(&multiplier))
        {
            if ((multiplicand.word[CALC_INT_WORDS - 1] & SIGN_BIT) != 0UL)
                return 1;
            shift_left(&multiplicand);
        }
    }
    return 0;
}

#ifdef Z80_ASM_OPTS
extern unsigned int xad10(struct CalcInt1024 *value);

static unsigned int unsigned_divide_small(struct CalcInt1024 *value,
                                          unsigned int divisor)
{
    return xad10(value);
}
#else
static unsigned int unsigned_divide_small(struct CalcInt1024 *value,
                                          unsigned int divisor)
{
    unsigned long source;
    unsigned long quotient;
    unsigned int partial;
    unsigned int remainder;
    int index;

    remainder = 0;
    index = CALC_INT_WORDS - 1;
    while (index >= 0 && value->word[index] == 0UL)
        --index;
    while (index >= 0)
    {
        source = value->word[index];
        partial = (remainder << 8) + (unsigned int)(source >> 24);
        quotient = (unsigned long)(partial / divisor) << 24;
        remainder = partial % divisor;
        partial = (remainder << 8) + (unsigned int)((source >> 16) & 0xffUL);
        quotient |= (unsigned long)(partial / divisor) << 16;
        remainder = partial % divisor;
        partial = (remainder << 8) + (unsigned int)((source >> 8) & 0xffUL);
        quotient |= (unsigned long)(partial / divisor) << 8;
        remainder = partial % divisor;
        partial = (remainder << 8) + (unsigned int)(source & 0xffUL);
        quotient |= (unsigned long)(partial / divisor);
        remainder = partial % divisor;
        value->word[index] = quotient;
        --index;
    }
    return remainder;
}
#endif

void xzero(struct CalcInt1024 *value)
{
    int index;

    for (index = 0; index < CALC_INT_WORDS; ++index)
        value->word[index] = 0UL;
}

void xfrom(struct CalcInt1024 *value, int source)
{
    unsigned long extension;
    int index;

    extension = source < 0 ? WORD_MAX : 0UL;
    value->word[0] = (unsigned long)(long)source;
    for (index = 1; index < CALC_INT_WORDS; ++index)
        value->word[index] = extension;
}

int xparse(const char *text, struct CalcInt1024 *value)
{
    struct CalcInt1024 ten;
    struct CalcInt1024 digit;
    struct CalcInt1024 product;
    struct CalcInt1024 maximum;
    int index;

    xzero(value);
    xfrom(&ten, 10);
    for (index = 0; index < CALC_INT_WORDS - 1; ++index)
        maximum.word[index] = WORD_MAX;
    maximum.word[CALC_INT_WORDS - 1] = 0x7fffffffUL;
    while (*text >= '0' && *text <= '9')
    {
        xfrom(&digit, *text - '0');
        if (unsigned_multiply_limited(value, &ten, &maximum, &product))
            return 1;
        unsigned_subtract(&maximum, &product, &maximum);
        if (unsigned_compare(&digit, &maximum) > 0)
            return 1;
        unsigned_add(&product, &digit, value);
        for (index = 0; index < CALC_INT_WORDS - 1; ++index)
            maximum.word[index] = WORD_MAX;
        maximum.word[CALC_INT_WORDS - 1] = 0x7fffffffUL;
        ++text;
    }
    return 0;
}

int xiszero(const struct CalcInt1024 *value)
{
    int index;

    for (index = 0; index < CALC_INT_WORDS; ++index)
        if (value->word[index] != 0UL)
            return 0;
    return 1;
}

int xisneg(const struct CalcInt1024 *value)
{
    return (value->word[CALC_INT_WORDS - 1] & SIGN_BIT) != 0UL;
}

int xcomp(const struct CalcInt1024 *left, const struct CalcInt1024 *right)
{
    int left_negative;
    int right_negative;

    left_negative = xisneg(left);
    right_negative = xisneg(right);
    if (left_negative != right_negative)
        return left_negative ? -1 : 1;
    return unsigned_compare(left, right);
}

int xnegate(const struct CalcInt1024 *value, struct CalcInt1024 *result)
{
    int index;

    if (value->word[CALC_INT_WORDS - 1] != SIGN_BIT)
    {
        unsigned_negate(value, result);
        return 0;
    }
    for (index = 0; index < CALC_INT_WORDS - 1; ++index)
        if (value->word[index] != 0UL)
        {
            unsigned_negate(value, result);
            return 0;
        }
    return 1;
}

void xnot(const struct CalcInt1024 *value, struct CalcInt1024 *result)
{
    int index;

    for (index = 0; index < CALC_INT_WORDS; ++index)
        result->word[index] = ~value->word[index];
}

int xadd(const struct CalcInt1024 *left, const struct CalcInt1024 *right,
         struct CalcInt1024 *result)
{
    int left_negative;
    int right_negative;

    left_negative = xisneg(left);
    right_negative = xisneg(right);
    unsigned_add(left, right, result);
    return left_negative == right_negative && xisneg(result) != left_negative;
}

int xsub(const struct CalcInt1024 *left, const struct CalcInt1024 *right,
         struct CalcInt1024 *result)
{
    int left_negative;
    int right_negative;

    left_negative = xisneg(left);
    right_negative = xisneg(right);
    unsigned_subtract(left, right, result);
    return left_negative != right_negative && xisneg(result) != left_negative;
}

int xmul(const struct CalcInt1024 *left, const struct CalcInt1024 *right,
         struct CalcInt1024 *result)
{
    struct CalcInt1024 left_magnitude;
    struct CalcInt1024 right_magnitude;
    struct CalcInt1024 limit;
    int negative;
    int index;

    negative = xisneg(left) != xisneg(right);
    magnitude(left, &left_magnitude);
    magnitude(right, &right_magnitude);
    for (index = 0; index < CALC_INT_WORDS - 1; ++index)
        limit.word[index] = negative ? 0UL : WORD_MAX;
    limit.word[CALC_INT_WORDS - 1] = negative ? SIGN_BIT : 0x7fffffffUL;
    if (unsigned_multiply_limited(&left_magnitude, &right_magnitude,
                                  &limit, result))
        return 1;
    if (negative)
        unsigned_negate(result, result);
    return 0;
}

int xdiv(const struct CalcInt1024 *left, const struct CalcInt1024 *right,
         struct CalcInt1024 *quotient, struct CalcInt1024 *remainder)
{
    struct CalcInt1024 left_magnitude;
    struct CalcInt1024 right_magnitude;
    int index;
    int minimum;
    int minus_one;

    if (xiszero(right))
        return 1;
    minimum = left->word[CALC_INT_WORDS - 1] == SIGN_BIT;
    for (index = 0; index < CALC_INT_WORDS - 1; ++index)
        if (left->word[index] != 0UL)
            minimum = 0;
    minus_one = 1;
    for (index = 0; index < CALC_INT_WORDS; ++index)
        if (right->word[index] != WORD_MAX)
            minus_one = 0;
    if (minimum && minus_one)
        return 2;
    magnitude(left, &left_magnitude);
    magnitude(right, &right_magnitude);
    unsigned_divide(&left_magnitude, &right_magnitude, quotient, remainder);
    if (xisneg(left) != xisneg(right))
        unsigned_negate(quotient, quotient);
    if (xisneg(left))
        unsigned_negate(remainder, remainder);
    return 0;
}

void xfmt(const struct CalcInt1024 *value, char *buffer)
{
    struct CalcInt1024 absolute;
    char digits[CALC_INT_DECIMAL_DIGITS];
    int count;

    if (xiszero(value))
    {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    magnitude(value, &absolute);
    count = 0;
    while (!xiszero(&absolute))
        digits[count++] = (char)('0' + unsigned_divide_small(&absolute, 10));
    if (xisneg(value))
        *buffer++ = '-';
    while (count > 0)
        *buffer++ = digits[--count];
    *buffer = '\0';
}