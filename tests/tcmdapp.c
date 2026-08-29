/* Five-unit table-driven command application. The table contains callbacks
 * implemented in other units and the parser/dispatcher share public state. */
#include <stdio.h>
#include "tcmd.h"

int main(void)
{
    struct CmdState state;

    state.value = 0;
    state.commands = 0;
    state.errors = 0;
    if (!cmdrun_run_script(&state, "set 5; add 3; mul 4; neg")) {
        printf("tcmdapp parse failed\n");
        return 1;
    }
    if (state.value != -32 || state.commands != 4 || state.errors != 0) {
        printf("tcmdapp failed: %d %d %d\n",
               state.value, state.commands, state.errors);
        return 1;
    }
    printf("tcmdapp completed with great success\n");
    return 0;
}
