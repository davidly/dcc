#include <stdio.h>

typedef volatile int VolatileInt;

struct Holder {
    int marker, * volatile field;
};

struct AnonymousHolder {
    volatile struct {
        int *field;
    };
};

static int first_values[2] = { 10, 20 };
static int second_values[2] = { 100, 200 };
static struct Holder holder = { 0, first_values };
static struct Holder *base;
static struct AnonymousHolder anonymous_holder = { first_values };
static struct AnonymousHolder *anonymous_base;
static VolatileInt typedef_value = -9;

static int callback_value(void)
{
    return 13;
}

typedef int (* volatile Callback)(void);
static Callback callback = callback_value;

static void setup_base(void)
{
    base = &holder;
}

static void switch_field(void)
{
    holder.field = second_values;
}

static void switch_anonymous_field(void)
{
    anonymous_holder.field = second_values;
}

static int volatile_param_abs(volatile int value)
{
    return value < 0 ? -value : value;
}

static int volatile_old_param_abs(value)
volatile int value;
{
    return value < 0 ? -value : value;
}

static unsigned int volatile_param_mask(volatile unsigned int value)
{
    return value & 7u;
}

static int volatile_static_abs(void)
{
    static volatile int value = -7;
    return value < 0 ? -value : value;
}

static int volatile_typedef_abs(void)
{
    return typedef_value < 0 ? -typedef_value : typedef_value;
}

static int const_volatile_read(void)
{
    const volatile int value = 11;
    return value;
}

static int volatile_local_widths(void)
{
    volatile int values[2];
    register volatile int scalar;
    volatile int counter;
    int sum;

    values[0] = 1;
    values[1] = 2;
    scalar = 3;
    sum = values[0] + values[1] + scalar;
    for (counter = 0; counter < 3; counter++)
        sum++;

    return sizeof values[0] == sizeof(int) &&
           sizeof scalar == sizeof(int) &&
           sizeof counter == sizeof(int) && sum == 9;
}

static int volatile_member_reload(void)
{
    int index = 0;
    int *item;
    int total = 0;

    for (; index < 2;) {
        item = &base->field[index++];
        total += *item;
        switch_field();
    }
    return total;
}

static int volatile_anonymous_member_reload(void)
{
    int index = 0;
    int *item;
    int total = 0;

    anonymous_base = &anonymous_holder;
    for (; index < 2;) {
        item = &anonymous_base->field[index++];
        total += *item;
        switch_anonymous_field();
    }
    return total;
}

int main(void)
{
    setup_base();
        printf("tvolopt param=%d old=%d mask=%u static=%d typedef=%d cv=%d local=%d member=%d anonymous=%d fp=%d\n",
            volatile_param_abs(-5), volatile_old_param_abs(-6),
            volatile_param_mask(0x1234u),
           volatile_static_abs(), volatile_typedef_abs(),
            const_volatile_read(), volatile_local_widths(), volatile_member_reload(),
            volatile_anonymous_member_reload(), callback());
    return 0;
}
