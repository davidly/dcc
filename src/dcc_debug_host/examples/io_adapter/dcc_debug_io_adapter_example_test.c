#include "dcc_debug_io_adapter.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    dcc_debug_io_adapter_config_t config;
    dcc_debug_io_adapter_t adapter;
    uint8_t output[8];
    char error[128];
    static const uint8_t left_arrow[] = {0x1b, '[', 'D'};
    static const uint8_t ordinary[] = {'A', 'B'};

    memset(&config, 0, sizeof(config));
    config.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.struct_size = sizeof(config);
    config.host.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.host.struct_size = sizeof(config.host);

    assert(dcc_debug_io_adapter_init(&config, &adapter, error, sizeof(error)));
    assert(adapter.input != NULL);
    assert(adapter.output != NULL);
    assert(adapter.close != NULL);
    assert(adapter.terminal_input != NULL);
    assert(adapter.terminal_poll != NULL);

    assert(adapter.terminal_input(adapter.context, ordinary, sizeof(ordinary),
                                  output, sizeof(output), 50) == 2);
    assert(output[0] == 'A' && output[1] == 'B');
    assert(adapter.terminal_input(adapter.context, left_arrow, 1,
                                  output, sizeof(output), 100) == 0);
    assert(adapter.terminal_input(adapter.context, left_arrow + 1, 1,
                                  output, sizeof(output), 101) == 0);
    assert(adapter.terminal_input(adapter.context, left_arrow + 2, 1,
                                  output, sizeof(output), 102) == 1);
    assert(output[0] == 0x13);

    assert(adapter.terminal_input(adapter.context, left_arrow, 1,
                                  output, sizeof(output), 200) == 0);
    assert(adapter.terminal_poll(adapter.context, output,
                                 sizeof(output), 229) == 0);
    assert(adapter.terminal_poll(adapter.context, output,
                                 sizeof(output), 230) == 1);
    assert(output[0] == 0x1b);

    adapter.close(adapter.context);
    return 0;
}
