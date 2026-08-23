#include "dcc_debug_io_adapter.h"

#include <stdio.h>
#include <string.h>

#ifndef DCC_TEST_ADAPTER_MODE
#define DCC_TEST_ADAPTER_MODE 0
#endif

static uint8_t fixture_input(void *context, uint8_t port)
{
    (void)context;
    return (uint8_t)(port ^ 0x5a);
}

static void fixture_output(void *context, uint8_t port, uint8_t data)
{
    (void)context;
    (void)port;
    (void)data;
}

static void fixture_close(void *context)
{
    (void)context;
}

static size_t oversized_terminal_input(
    void *context, const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size, uint64_t now_ms)
{
    (void)context;
    (void)input;
    (void)input_size;
    (void)now_ms;
    if (output_size != 0)
        output[0] = 0xa5;
    return output_size + 7;
}

static size_t oversized_terminal_poll(
    void *context, uint8_t *output, size_t output_size, uint64_t now_ms)
{
    (void)context;
    (void)now_ms;
    if (output_size != 0)
        output[0] = 0x5a;
    return output_size + 3;
}

int dcc_debug_io_adapter_init(const dcc_debug_io_adapter_config_t *config,
                              dcc_debug_io_adapter_t *adapter,
                              char *error, size_t error_size)
{
    if (DCC_TEST_ADAPTER_MODE == 1)
    {
        if (error != NULL && error_size != 0)
            snprintf(error, error_size, "fixture init failure");
        return 0;
    }
    if (config == NULL || adapter == NULL)
        return 0;

    memset(adapter, 0, sizeof(*adapter));
    adapter->abi_version = DCC_TEST_ADAPTER_MODE == 2 ?
                           DCC_DEBUG_IO_ADAPTER_ABI_VERSION + 1 :
                           DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    adapter->struct_size = sizeof(*adapter);
    adapter->input = DCC_TEST_ADAPTER_MODE == 3 ? NULL : fixture_input;
    adapter->output = fixture_output;
    adapter->close = fixture_close;
    if (DCC_TEST_ADAPTER_MODE == 4)
    {
        adapter->terminal_input = oversized_terminal_input;
        adapter->terminal_poll = oversized_terminal_poll;
    }
    return 1;
}