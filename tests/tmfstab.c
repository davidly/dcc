#include "tmfstat.h"

static int overlapping_static_value = 9;

static int overlapping_static_function(int value)
{
    return value + overlapping_static_value;
}

int mf_tentative_value;

int beta_translation_unit_long_name(int value)
{
    return overlapping_static_function(value) + mf_tentative_value;
}
