#ifndef DCC_DEBUG_HARDWARE_Z80_H
#define DCC_DEBUG_HARDWARE_Z80_H

#include "types.h"

#define FLAGS_CARRY		0x1
#define FLAGS_PARITY		0x4
#define FLAGS_H			16
#define FLAGS_IF		32
#define FLAGS_ZERO		64
#define FLAGS_SIGN		128

typedef struct
{
	union
	{
		uint16_t af;

		struct {
			uint8_t flags;
			uint8_t a;
		};
	};

	union
	{
		uint16_t bc;
		struct
		{
			uint8_t c;
			uint8_t b;
		};
	};

	union
	{
		uint16_t de;
		struct
		{
			uint8_t e;
			uint8_t d;

		};
	};

	union
	{
		uint16_t hl;
		struct
		{
			uint8_t l;
			uint8_t h;
		};
	};

	uint16_t sp;
	uint16_t pc;
} registers_t;

typedef void (*io_port_out_fn)(uint8_t port, uint8_t data);
typedef uint8_t (*io_port_in_fn)(uint8_t port);

typedef void (*port_out)(uint8_t b);
typedef uint8_t (*port_in)(void);
typedef uint8_t (*read_sense_switches)(void);

typedef struct
{
	port_out disk_select;
	port_in	disk_status;
	port_out disk_function;
	port_in sector;
	port_out write;
	port_in read;
} disk_controller_t;

typedef struct
{
	uint8_t data_bus;
	uint16_t address_bus;

	uint8_t display_data_bus;
	uint16_t display_address_bus;
	uint8_t display_cpuStatus;
	uint32_t display_event_snapshot;

	uint8_t current_op_code;

	registers_t registers;

	io_port_in_fn io_port_in_handler;
	io_port_out_fn io_port_out_handler;

	port_in term_in;
	port_out term_out;
	read_sense_switches sense;
	uint8_t cpuStatus;

	bool halted;	// True when CPU is halted by HLT instruction
	bool iff;	// IFF1 interrupt enable flip-flop. Not part of PSW.
	bool iff2;	// IFF2 saved interrupt enable flip-flop.
	uint8_t interrupt_mode;

	disk_controller_t disk_controller;
} z80_t;

void z80_reset(z80_t *cpu, port_in in, port_out out, read_sense_switches sense,
		 disk_controller_t *disk_controller, io_port_in_fn io_in, io_port_out_fn io_out);
void z80_deposit(z80_t *cpu, uint8_t data);
void z80_deposit_next(z80_t *cpu, uint8_t data);

void z80_examine(z80_t *cpu, uint16_t address);
void z80_examine_next(z80_t *cpu);

/* Resume execution after a HLT. Clears the halted latch and the HLTA
   status bit so the front panel reflects the new state immediately. */
void z80_resume(z80_t *cpu);

/* Accept a maskable interrupt if IFF1 is enabled. data_bus is the device
	supplied byte used by interrupt modes 0 and 2. Returns true if accepted. */
bool z80_interrupt(z80_t *cpu, uint8_t data_bus);

/* Consume a transient interrupt-acknowledge snapshot for front-panel display.
   Returns false when no event has occurred since the previous call. */
bool z80_take_display_event(z80_t *cpu, uint16_t *address_bus,
			   uint8_t *data_bus, uint8_t *cpu_status);

void z80_cycle(z80_t *cpu);
void z80_execute_instructions(z80_t *cpu, uint16_t instruction_count);

#endif
