#ifndef DCC_DEBUG_HOST_Z80_H
#define DCC_DEBUG_HOST_Z80_H

/* Debugger-local extension of the internal Z80 hardware API.
	The engine and adapter are private to dcc_debug_host; the hardware-facing
   z80_t and disk controller ABI remain shared with the disk/interrupt code. */

#include "hardware/z80.h"

typedef struct
{
	uint16_t af;
	uint16_t bc;
	uint16_t de;
	uint16_t hl;
	uint16_t ix;
	uint16_t iy;
	uint16_t sp;
	uint16_t pc;
	uint16_t af_alt;
	uint16_t bc_alt;
	uint16_t de_alt;
	uint16_t hl_alt;
	uint8_t i;
	uint8_t r;
} z80_debug_registers_t;

void z80_debug_get_registers(z80_debug_registers_t *registers);
bool z80_debug_set_location_register(uint8_t location, uint32_t value);
uint8_t z80_debug_instruction_length(uint16_t address);
const char *z80_debug_disassemble(uint16_t address);

#endif
