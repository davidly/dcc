#include <string.h>
#include "tcmd.h"

struct CmdEntry cmdtab_command_table[] = {
    { "set", cmdset_set_value, 1 },
    { "add", cmdadd_add_value, 1 },
    { "mul", cmdmul_multiply_value, 1 },
    { "neg", cmdneg_negate_value, 0 }
};

int cmdcnt_command_count =
    sizeof(cmdtab_command_table) / sizeof(cmdtab_command_table[0]);

int cmddis_dispatch(struct CmdState *state, const char *name, int argument,
                    int has_argument)
{
    int index;

    for (index = 0; index < cmdcnt_command_count; ++index) {
        if (strcmp(cmdtab_command_table[index].name, name) == 0) {
            if (cmdtab_command_table[index].needs_argument != has_argument)
                return 0;
            return cmdtab_command_table[index].action(state, argument);
        }
    }
    return 0;
}
