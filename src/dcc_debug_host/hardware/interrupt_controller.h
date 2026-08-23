#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "z80.h"

#define INTERRUPT_CONTROLLER_MAX_PROVIDERS 8
#define INTERRUPT_PROVIDER_INVALID 0xff

typedef uint8_t interrupt_provider_id_t;
typedef void (*interrupt_provider_poll_fn)(void *context);

typedef struct
{
     /* Byte supplied during interrupt acknowledge. Mode 1 ignores this;
         modes 0 and 2 use it as defined by the Z80. */
    uint8_t data_bus;
     /* Optional polling hook, used by host providers without async callbacks. */
    interrupt_provider_poll_fn poll;
    void *context;
} interrupt_provider_config_t;

/* Register providers during emulator initialization. Earlier registrations
    have higher priority when more than one provider is pending. */
void interrupt_controller_init(void);
bool interrupt_controller_register(const interrupt_provider_config_t *config,
                                   interrupt_provider_id_t *provider_id);
/* raise() is safe across emulator cores/callbacks and counts repeated events.
    A request remains pending until accepted or explicitly cleared. */
void interrupt_controller_raise(interrupt_provider_id_t provider_id);
void interrupt_controller_clear(interrupt_provider_id_t provider_id);
/* Poll providers and present the highest-priority pending request to the CPU. */
bool interrupt_controller_service(z80_t *cpu);