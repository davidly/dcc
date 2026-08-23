#include "dcc_debug_io_adapter.h"

#include <stdio.h>
#include <string.h>

#define ESCAPE_GRACE_MS 30u
#define CONTROL_KEY(character) ((uint8_t)((character) & 0x1f))

typedef enum terminal_state
{
    TERMINAL_NORMAL,
    TERMINAL_ESCAPE,
    TERMINAL_CSI
} terminal_state_t;

typedef struct example_context
{
    terminal_state_t terminal_state;
    uint64_t escape_start_ms;
} example_context_t;

static example_context_t example_context;

static uint8_t example_port_input(void *context, uint8_t port)
{
    (void)context;
    (void)port;
    return 0;
}

static void example_port_output(void *context, uint8_t port, uint8_t data)
{
    (void)context;
    (void)port;
    (void)data;
}

static uint8_t process_terminal_byte(example_context_t *context,
                                     uint8_t input, uint64_t now_ms)
{
    switch (context->terminal_state)
    {
        case TERMINAL_NORMAL:
            if (input == 0x1b)
            {
                context->terminal_state = TERMINAL_ESCAPE;
                context->escape_start_ms = now_ms;
                return 0;
            }
            return input;

        case TERMINAL_ESCAPE:
            if (input == 0)
            {
                if (now_ms - context->escape_start_ms >= ESCAPE_GRACE_MS)
                {
                    context->terminal_state = TERMINAL_NORMAL;
                    return 0x1b;
                }
                return 0;
            }
            if (input == '[')
            {
                context->terminal_state = TERMINAL_CSI;
                return 0;
            }
            context->terminal_state = TERMINAL_NORMAL;
            return input;

        case TERMINAL_CSI:
            if (input == 0)
                return 0;
            context->terminal_state = TERMINAL_NORMAL;
            /* Example policy only: replace these values with the control
               bytes expected by the target system or application. */
            switch (input)
            {
                case 'A': return CONTROL_KEY('E');
                case 'B': return CONTROL_KEY('X');
                case 'C': return CONTROL_KEY('D');
                case 'D': return CONTROL_KEY('S');
                default: return 0;
            }
    }
    context->terminal_state = TERMINAL_NORMAL;
    return 0;
}

static size_t example_terminal_input(
    void *opaque_context,
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size,
    uint64_t now_ms)
{
    example_context_t *context = (example_context_t *)opaque_context;
    size_t input_index;
    size_t output_count = 0;

    if (input == NULL || output == NULL || output_size == 0)
        return 0;
    for (input_index = 0; input_index < input_size; ++input_index)
    {
        uint8_t translated = process_terminal_byte(context, input[input_index], now_ms);
        if (translated != 0 && output_count < output_size)
            output[output_count++] = translated;
    }
    return output_count;
}

static size_t example_terminal_poll(
    void *opaque_context,
    uint8_t *output, size_t output_size,
    uint64_t now_ms)
{
    example_context_t *context = (example_context_t *)opaque_context;
    uint8_t translated;

    if (output == NULL || output_size == 0)
        return 0;
    translated = process_terminal_byte(context, 0, now_ms);
    if (translated == 0)
        return 0;
    output[0] = translated;
    return 1;
}

static void example_close(void *context)
{
    if (context != NULL)
        memset(context, 0, sizeof(example_context));
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0)
        snprintf(error, error_size, "%s", message);
}

int dcc_debug_io_adapter_init(const dcc_debug_io_adapter_config_t *config,
                              dcc_debug_io_adapter_t *adapter,
                              char *error, size_t error_size)
{
    if (config == NULL || adapter == NULL)
    {
        set_error(error, error_size, "config and adapter are required");
        return 0;
    }
    if (config->abi_version != DCC_DEBUG_IO_ADAPTER_ABI_VERSION ||
        config->struct_size < sizeof(*config) ||
        config->host.abi_version != DCC_DEBUG_IO_ADAPTER_ABI_VERSION ||
        config->host.struct_size < sizeof(config->host))
    {
        set_error(error, error_size, "unsupported I/O adapter ABI version");
        return 0;
    }

    memset(&example_context, 0, sizeof(example_context));
    memset(adapter, 0, sizeof(*adapter));
    adapter->abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    adapter->struct_size = sizeof(*adapter);
    adapter->context = &example_context;
    adapter->input = example_port_input;
    adapter->output = example_port_output;
    adapter->close = example_close;
    adapter->terminal_input = example_terminal_input;
    adapter->terminal_poll = example_terminal_poll;
    if (error != NULL && error_size != 0)
        error[0] = '\0';
    return 1;
}
