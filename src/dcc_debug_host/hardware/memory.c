#include "memory.h"
#include <string.h>

// Emulated system memory - 64KB
uint8_t memory[64 * 1024] = {0};

// ROM data stored in flash (XIP)
#include "88dskrom.h"
#include "8krom.h"

// Load disk boot loader ROM into memory at specified address
void loadDiskLoader(uint16_t address)
{
    // Copy ROM data from flash to RAM
    memcpy(&memory[address], disk_loader_rom, sizeof(disk_loader_rom));
}

// Load 8K BASIC ROM into memory at specified address
void load8kRom(uint16_t address)
{
    // Copy ROM data from flash to RAM
    memcpy(&memory[address], basic_8k_rom, sizeof(basic_8k_rom));
}
