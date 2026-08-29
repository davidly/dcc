#include "tmfcall.h"

extern int callback_reached_only_through_table_alpha(int value);

static int callback_private_bias = 4;

static int callback_reached_only_through_table_beta(int value)
{
    return value * callback_private_bias;
}

MfCallback mf_callback_table[2] = {
    callback_reached_only_through_table_alpha,
    callback_reached_only_through_table_beta
};

int mf_dispatch_callback(int index, int value)
{
    return mf_callback_table[index](value);
}
