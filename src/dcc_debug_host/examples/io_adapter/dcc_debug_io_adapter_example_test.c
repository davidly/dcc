#include "dcc_debug_io_adapter.h"

#include <assert.h>
#include <string.h>

typedef struct test_host
{
    dcc_debug_io_interrupt_poll_fn poll;
    void *poll_context;
    unsigned int raise_count;
    unsigned int clear_count;
    uint8_t data_bus;
} test_host_t;

static int register_interrupt(void *context, uint8_t data_bus,
                              dcc_debug_io_interrupt_poll_fn poll,
                              void *poll_context,
                              dcc_debug_io_interrupt_id_t *interrupt_id)
{
    test_host_t *host = (test_host_t *)context;

    host->poll = poll;
    host->poll_context = poll_context;
    host->data_bus = data_bus;
    *interrupt_id = 7;
    return 1;
}

static void raise_interrupt(void *context,
                            dcc_debug_io_interrupt_id_t interrupt_id)
{
    test_host_t *host = (test_host_t *)context;

    assert(interrupt_id == 7);
    host->raise_count++;
}

static void clear_interrupt(void *context,
                            dcc_debug_io_interrupt_id_t interrupt_id)
{
    test_host_t *host = (test_host_t *)context;

    assert(interrupt_id == 7);
    host->clear_count++;
}

int main(void)
{
    dcc_debug_io_adapter_config_t config;
    dcc_debug_io_adapter_t adapter;
    test_host_t host;
    uint8_t output[8];
    char error[128];
    unsigned long attempt;
    static const uint8_t left_arrow[] = {0x1b, '[', 'D'};
    static const uint8_t ordinary[] = {'A', 'B'};

    memset(&config, 0, sizeof(config));
    memset(&host, 0, sizeof(host));
    config.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.struct_size = sizeof(config);
    config.host.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.host.struct_size = sizeof(config.host);
    config.host.context = &host;
    config.host.register_interrupt = register_interrupt;
    config.host.raise_interrupt = raise_interrupt;
    config.host.clear_interrupt = clear_interrupt;

    assert(dcc_debug_io_adapter_init(&config, &adapter, error, sizeof(error)));
    assert(host.poll != NULL);
    assert(host.poll_context != NULL);
    assert(host.data_bus == 0xff);
    assert(adapter.input != NULL);
    assert(adapter.output != NULL);
    assert(adapter.close != NULL);
    assert(adapter.terminal_input != NULL);
    assert(adapter.terminal_poll != NULL);

    adapter.output(adapter.context, 24, 0xff);
    adapter.output(adapter.context, 25, 0xff);
    assert(adapter.input(adapter.context, 24) == 1);
    assert(adapter.input(adapter.context, 25) == 1);

    adapter.output(adapter.context, 26, 0);
    adapter.output(adapter.context, 27, 0);
    assert(adapter.input(adapter.context, 26) == 0);

    adapter.output(adapter.context, 30, 255);
    assert(adapter.input(adapter.context, 30) == 1);
    assert(adapter.input(adapter.context, 99) == 0);

    adapter.output(adapter.context, 52, 255);
    assert(adapter.input(adapter.context, 52) == 255);
    for (attempt = 0; attempt < 10000000UL && host.raise_count == 0; ++attempt)
        host.poll(host.poll_context);
    assert(host.raise_count != 0);
    adapter.output(adapter.context, 52, 0);
    assert(adapter.input(adapter.context, 52) == 0);
    assert(host.clear_count == 2);

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
    assert(host.clear_count == 3);
    return 0;
}
