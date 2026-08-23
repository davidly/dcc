#include "interrupt_controller.h"

#include <limits.h>
#include <stdatomic.h>
#include <string.h>

typedef struct
{
    interrupt_provider_config_t config;
    atomic_uint pending_count;
} interrupt_provider_slot_t;

static interrupt_provider_slot_t providers[INTERRUPT_CONTROLLER_MAX_PROVIDERS];
static uint8_t provider_count;

void interrupt_controller_init(void)
{
    memset(providers, 0, sizeof(providers));
    provider_count = 0;
}

bool interrupt_controller_register(const interrupt_provider_config_t *config,
                                   interrupt_provider_id_t *provider_id)
{
    interrupt_provider_slot_t *slot;

    if (config == NULL || provider_id == NULL ||
        provider_count >= INTERRUPT_CONTROLLER_MAX_PROVIDERS)
    {
        return false;
    }

    slot = &providers[provider_count];
    slot->config = *config;
    atomic_store_explicit(&slot->pending_count, 0, memory_order_relaxed);
    *provider_id = provider_count++;
    return true;
}

void interrupt_controller_raise(interrupt_provider_id_t provider_id)
{
    unsigned int count;

    if (provider_id >= provider_count)
        return;

    count = atomic_load_explicit(&providers[provider_id].pending_count,
                                 memory_order_relaxed);
    while (count != UINT_MAX &&
           !atomic_compare_exchange_weak_explicit(&providers[provider_id].pending_count,
                                                  &count, count + 1,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed))
    {
    }
}

void interrupt_controller_clear(interrupt_provider_id_t provider_id)
{
    if (provider_id < provider_count)
        atomic_store_explicit(&providers[provider_id].pending_count, 0,
                              memory_order_relaxed);
}

static void consume(interrupt_provider_slot_t *slot)
{
    unsigned int count = atomic_load_explicit(&slot->pending_count,
                                              memory_order_relaxed);
    while (count != 0 &&
           !atomic_compare_exchange_weak_explicit(&slot->pending_count, &count,
                                                  count - 1,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed))
    {
    }
}

bool interrupt_controller_service(z80_t *cpu)
{
    uint8_t index;

    for (index = 0; index < provider_count; ++index)
    {
        if (providers[index].config.poll != NULL)
            providers[index].config.poll(providers[index].config.context);
    }

    for (index = 0; index < provider_count; ++index)
    {
        interrupt_provider_slot_t *slot = &providers[index];
        if (atomic_load_explicit(&slot->pending_count, memory_order_relaxed) != 0 &&
            z80_interrupt(cpu, slot->config.data_bus))
        {
            consume(slot);
            return true;
        }
    }

    return false;
}