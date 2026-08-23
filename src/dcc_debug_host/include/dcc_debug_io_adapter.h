#ifndef DCC_DEBUG_IO_ADAPTER_H
#define DCC_DEBUG_IO_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifdef DCC_DEBUG_IO_ADAPTER_BUILD
#define DCC_DEBUG_IO_ADAPTER_EXPORT __declspec(dllexport)
#else
#define DCC_DEBUG_IO_ADAPTER_EXPORT __declspec(dllimport)
#endif
#else
#define DCC_DEBUG_IO_ADAPTER_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DCC_DEBUG_IO_ADAPTER_ABI_VERSION 2u
#define DCC_DEBUG_IO_ADAPTER_INIT_SYMBOL "dcc_debug_io_adapter_init"

typedef uint8_t dcc_debug_io_interrupt_id_t;
typedef void (*dcc_debug_io_interrupt_poll_fn)(void *context);

typedef struct dcc_debug_io_host_services
{
    uint32_t abi_version;
    size_t struct_size;
    void *context;
    int (*register_interrupt)(void *context, uint8_t data_bus,
                              dcc_debug_io_interrupt_poll_fn poll,
                              void *poll_context,
                              dcc_debug_io_interrupt_id_t *interrupt_id);
    void (*raise_interrupt)(void *context,
                            dcc_debug_io_interrupt_id_t interrupt_id);
    void (*clear_interrupt)(void *context,
                            dcc_debug_io_interrupt_id_t interrupt_id);
} dcc_debug_io_host_services_t;

typedef struct dcc_debug_io_adapter_config
{
    uint32_t abi_version;
    size_t struct_size;
    const char *environment_file;
    const char *files_root;
    dcc_debug_io_host_services_t host;
} dcc_debug_io_adapter_config_t;

typedef struct dcc_debug_io_adapter
{
    uint32_t abi_version;
    size_t struct_size;
    void *context;
    uint8_t (*input)(void *context, uint8_t port);
    void (*output)(void *context, uint8_t port, uint8_t data);
    void (*close)(void *context);
     /* Optional terminal filter. It may buffer input and emit zero or more
         bytes, but must never return more than output_size. */
    size_t (*terminal_input)(void *context,
                             const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             uint64_t now_ms);
     /* Optional idle poll for timeout-dependent output such as a standalone
         Escape key. It must never return more than output_size. */
    size_t (*terminal_poll)(void *context,
                            uint8_t *output, size_t output_size,
                            uint64_t now_ms);
} dcc_debug_io_adapter_t;

typedef int (*dcc_debug_io_adapter_init_fn)(
    const dcc_debug_io_adapter_config_t *config,
    dcc_debug_io_adapter_t *adapter,
    char *error,
    size_t error_size);

DCC_DEBUG_IO_ADAPTER_EXPORT int dcc_debug_io_adapter_init(
    const dcc_debug_io_adapter_config_t *config,
    dcc_debug_io_adapter_t *adapter,
    char *error,
    size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
