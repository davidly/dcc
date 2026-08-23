#ifndef _MEMORY_H_
#define _MEMORY_H_

#include "types.h"

extern uint8_t memory[64 * 1024];

void loadDiskLoader(uint16_t address);
void load8kRom(uint16_t address);

// Inline memory operations for better performance
static inline uint8_t read8(uint16_t address)
{
    return memory[address];
}

static inline void write8(uint16_t address, uint8_t val)
{
    memory[address] = val;
}

static inline uint16_t read16(uint16_t address)
{
    return memory[address] | (memory[address + 1] << 8);
}

static inline void write16(uint16_t address, uint16_t val)
{
    memory[address] = val & 0xff;
    memory[address + 1] = (val >> 8) & 0xff;
}

#endif
