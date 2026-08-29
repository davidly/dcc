#include "tcmd.h"

static int command_private_adjustment = 1;

int cmdset_set_value(struct CmdState *state, int argument)
{
    state->value = argument;
    return 1;
}

int cmdadd_add_value(struct CmdState *state, int argument)
{
    state->value += argument + command_private_adjustment - 1;
    return 1;
}

int cmdmul_multiply_value(struct CmdState *state, int argument)
{
    state->value *= argument;
    return 1;
}
