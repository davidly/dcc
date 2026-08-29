#include "tmfstat.h"

static int overlapping_static_value = 5;

static int overlapping_static_function(int value)
{
    return value + overlapping_static_value;
}

int mf_initialized_value = 20;

int alpha_translation_unit_long_name(int value)
{
    return overlapping_static_function(value) + mf_initialized_value;
}
