#include "tcmd.h"

static int command_private_adjustment = 0;

int cmdneg_negate_value(struct CmdState *state, int argument)
{
    (void)argument;
    state->value = -state->value + command_private_adjustment;
    return 1;
}
