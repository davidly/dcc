#include "tmfcall.h"

static int callback_private_bias = 6;

int callback_reached_only_through_table_alpha(int value)
{
    return value + callback_private_bias;
}
