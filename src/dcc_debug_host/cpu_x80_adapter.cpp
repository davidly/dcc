// Z80 CPU adapter dedicated to dcc_debug_host.
// Its engine/register model is the debugger-local Z80-only ntvcm derivative;
// other emulators use their own adapter/core copies.
//
// The debugger host talks to the CPU through the C API declared in z80.h:
// z80_reset, z80_cycle,
// z80_examine/deposit, etc., reading register/bus state back out of an
// z80_t struct.
//
// This file implements that exact API on top of the ntvcm-derived Z80 core in
// x80.cxx. The core keeps its CPU state in the global
// `reg` and shares the global `memory[]` with memory.c, so the adapter syncs
// `reg` back into the caller's z80_t after each step and supplies the
// I/O / halt / hook callbacks the core calls out to.
//
// This adapter (plus x80.cxx) is the project's single CPU core; the front
// panel and main loop talk only to the z80_* API declared in z80.h.

extern "C"
{
#include "z80.h"
}

#include <cstddef>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "x80.hxx"
#include <djltrace.hxx>

static_assert(registers::z80_only, "debugger adapter requires a Z80-only engine");

// Front-panel status LED bits.
#define STATUS_MEMORY_READ   0x80
#define STATUS_PORT_INPUT    0x40
#define STATUS_OP_CODE_FETCH 0x20
#define STATUS_PORT_OUTPUT   0x10
#define STATUS_HALT          0x08
#define STATUS_STACK         0x04
#define STATUS_WRITE_OUTPUT  0x02
#define STATUS_INTERRUPT     0x01

// No-op tracer instance required by x80.cxx (instruction tracing stays off).
CDJLTrace tracer;

// The x80 core uses a single global CPU state, so the adapter likewise tracks a
// single active CPU instance. Its term/disk/sense/io callbacks are read back
// out of this struct from the I/O callbacks below.
static z80_t *g_cpu = NULL;

static void store_display_event(uint32_t *destination, uint32_t value)
{
#ifdef _MSC_VER
    InterlockedExchange(reinterpret_cast<volatile LONG *>(destination),
                        static_cast<LONG>(value));
#else
    __atomic_store_n(destination, value, __ATOMIC_RELEASE);
#endif
}

static uint32_t take_display_event(uint32_t *destination)
{
#ifdef _MSC_VER
    return static_cast<uint32_t>(InterlockedExchange(
        reinterpret_cast<volatile LONG *>(destination), 0));
#else
    return __atomic_exchange_n(destination, 0, __ATOMIC_ACQUIRE);
#endif
}

// Copy the x80 core's live registers (global `reg`) into the caller's struct so
// the front panel and host tests observe up-to-date register and flag state.
static void sync_regs_out(z80_t *cpu)
{
    cpu->registers.a = reg.a;
    cpu->registers.flags = reg.materializeFlags();
    cpu->registers.b = reg.b;
    cpu->registers.c = reg.c;
    cpu->registers.d = reg.d;
    cpu->registers.e = reg.e;
    cpu->registers.h = reg.h;
    cpu->registers.l = reg.l;
    cpu->registers.sp = reg.sp;
    cpu->registers.pc = reg.pc;
    cpu->iff = reg.fINTE;
    cpu->iff2 = reg.fINTE2;
    cpu->interrupt_mode = reg.interruptMode;
}

#ifdef DCC_DEBUG_X80_HOST_TEST
// Host-test builds drive the CPU through a CP/M BDOS trap that pokes pc/sp (and
// occasionally other registers) straight into the z80_t between steps.
// Mirror those edits back into the core's global `reg` before each instruction.
// The production emulator never needs this (front-panel edits go through
// z80_examine, which updates `reg` directly), so it stays out of the hot path.
static void sync_regs_in(z80_t *cpu)
{
    reg.a = cpu->registers.a;
    reg.b = cpu->registers.b;
    reg.c = cpu->registers.c;
    reg.d = cpu->registers.d;
    reg.e = cpu->registers.e;
    reg.h = cpu->registers.h;
    reg.l = cpu->registers.l;
    reg.sp = cpu->registers.sp;
    reg.pc = cpu->registers.pc;
    reg.f = cpu->registers.flags;
    reg.unmaterializeFlags();
}
#endif

static void update_display_bus(z80_t *cpu)
{
    cpu->display_address_bus = cpu->address_bus;
    cpu->display_data_bus = cpu->data_bus;
    cpu->display_cpuStatus = cpu->cpuStatus;
}

static uint32_t pack_display_event(uint16_t address_bus, uint8_t data_bus,
                                   uint8_t cpu_status)
{
    return (uint32_t)address_bus |
           ((uint32_t)data_bus << 16) |
           ((uint32_t)cpu_status << 24);
}

static bool opcode_may_move_stack(uint8_t opcode)
{
    return (opcode & 0xc7) == 0xc0 || // conditional RET
           (opcode & 0xc7) == 0xc4 || // conditional CALL
           (opcode & 0xcf) == 0xc1 || // POP
           (opcode & 0xcf) == 0xc5 || // PUSH
           (opcode & 0xc7) == 0xc7 || // RST
           opcode == 0xc9 || opcode == 0xcd ||
           opcode == 0xdd || opcode == 0xed || opcode == 0xfd;
}

extern "C" void z80_reset(z80_t *cpu, port_in in, port_out out, read_sense_switches sense,
                            disk_controller_t *disk_controller, io_port_in_fn io_in, io_port_out_fn io_out)
{
    memset(cpu, 0, sizeof(z80_t));
    cpu->term_in = in;
    cpu->term_out = out;
    cpu->io_port_in_handler = io_in;
    cpu->io_port_out_handler = io_out;
    cpu->disk_controller = *disk_controller;
    cpu->sense = sense;
    cpu->cpuStatus = 0x00;
    cpu->halted = false;
    cpu->iff = false;

    g_cpu = cpu;

    // Zero-initialize the core's registers. The boot loader address is loaded
    // subsequently via z80_examine().
    reg = registers();
    reg.pc = 0;

    sync_regs_out(cpu);
}

void z80_resume(z80_t *cpu)
{
    cpu->halted = false;
    cpu->cpuStatus &= ~STATUS_HALT;
}

extern "C" bool z80_interrupt(z80_t *cpu, uint8_t data_bus)
{
    // A Z80 does not accept a maskable interrupt until the instruction after
    // EI. The core already publishes the final opcode in each execution batch,
    // avoiding an always-zero delay check in the per-instruction hot path.
    if (!reg.fINTE || x80_last_opcode == 0xfb)
        return false;
    if (reg.interruptMode == 0 && (data_bus & 0xc7) != 0xc7)
        return false;

    g_cpu = cpu;
    z80_resume(cpu);
    reg.fINTE = false;
    reg.fINTE2 = false;
    reg.sp -= 2;
    memory[reg.sp] = (uint8_t)reg.pc;
    memory[(uint16_t)(reg.sp + 1)] = (uint8_t)(reg.pc >> 8);

    if (reg.interruptMode == 2)
    {
        uint16_t vector = ((uint16_t)reg.i << 8) | data_bus;
        reg.pc = memory[vector] | ((uint16_t)memory[(uint16_t)(vector + 1)] << 8);
        reg.z80_set_memptr(reg.pc);
    }
    else
    {
        // Mode 1 always executes RST 38h. In mode 0 this emulator accepts the
        // RST opcode normally supplied by a mode-0 interrupt device.
        uint8_t rst = reg.interruptMode == 1 ? 0xff : data_bus;
        reg.pc = rst & 0x38;
        reg.z80_set_memptr(reg.pc);
    }

    cpu->cpuStatus = STATUS_INTERRUPT | STATUS_STACK;
    sync_regs_out(cpu);
    cpu->address_bus = reg.pc;
    cpu->data_bus = memory[reg.pc];
    update_display_bus(cpu);
    store_display_event(&cpu->display_event_snapshot,
                        pack_display_event(cpu->address_bus, cpu->data_bus,
                                           cpu->cpuStatus));
    return true;
}

extern "C" bool z80_take_display_event(z80_t *cpu, uint16_t *address_bus,
                                         uint8_t *data_bus, uint8_t *cpu_status)
{
    uint32_t snapshot = take_display_event(&cpu->display_event_snapshot);
    if (snapshot == 0)
        return false;

    *address_bus = (uint16_t)snapshot;
    *data_bus = (uint8_t)(snapshot >> 16);
    *cpu_status = (uint8_t)(snapshot >> 24);
    return true;
}

void z80_execute_instructions(z80_t *cpu, uint16_t instruction_count)
{
    if (cpu->halted || instruction_count == 0)
    {
        // Keep HLTA asserted while halted, matching real-8080 behavior.
        if (cpu->halted)
        {
            cpu->cpuStatus = STATUS_HALT;
            cpu->display_cpuStatus = STATUS_HALT;
        }
        return;
    }

    g_cpu = cpu;

#ifdef DCC_DEBUG_X80_HOST_TEST
    sync_regs_in(cpu);
#endif

    cpu->cpuStatus = STATUS_MEMORY_READ | STATUS_OP_CODE_FETCH;

    x80_emulate_instructions(instruction_count);

    // RUN mode samples only the final instruction in a batch. The core keeps
    // the sample local while executing and publishes it once at batch exit, so
    // transient LED activity is not accumulated across the whole batch.
    cpu->cpuStatus = STATUS_MEMORY_READ | STATUS_OP_CODE_FETCH;
    if (x80_last_io_status & X80_IO_INPUT)
        cpu->cpuStatus |= STATUS_PORT_INPUT;
    if (x80_last_io_status & X80_IO_OUTPUT)
        cpu->cpuStatus |= STATUS_PORT_OUTPUT | STATUS_WRITE_OUTPUT;

    // push/pop/call/ret/rst are the only ops that move SP, always by 2;
    // cheaper than decoding the opcode to detect a stack access.
    if (opcode_may_move_stack(x80_last_opcode))
    {
        uint16_t sp_delta = reg.sp - x80_last_sp_before;
        if (sp_delta == 2 || sp_delta == (uint16_t)-2)
            cpu->cpuStatus |= STATUS_STACK;
    }

    if (cpu->halted)
        cpu->cpuStatus = STATUS_HALT;

    sync_regs_out(cpu);

    // Present the next fetch on the bus for the front panel.
    cpu->address_bus = reg.pc;
    cpu->data_bus = memory[reg.pc];
    update_display_bus(cpu);
}

void z80_cycle(z80_t *cpu)
{
    z80_execute_instructions(cpu, 1);
}

extern "C" void z80_debug_get_registers(z80_debug_registers_t *registers)
{
    if (!registers)
        return;
    registers->af = reg.PSW();
    registers->bc = reg.B();
    registers->de = reg.D();
    registers->hl = reg.H();
    registers->ix = reg.ix;
    registers->iy = reg.iy;
    registers->sp = reg.sp;
    registers->pc = reg.pc;
    registers->af_alt = (uint16_t)(((uint16_t)reg.ap << 8) | reg.fp);
    registers->bc_alt = (uint16_t)(((uint16_t)reg.bp << 8) | reg.cp);
    registers->de_alt = (uint16_t)(((uint16_t)reg.dp << 8) | reg.ep);
    registers->hl_alt = (uint16_t)(((uint16_t)reg.hp << 8) | reg.lp);
    registers->i = reg.i;
    registers->r = reg.r;
}

extern "C" bool z80_debug_set_location_register(uint8_t location, uint32_t value)
{
    switch (location)
    {
    case 2: reg.l = (uint8_t)value; reg.h = (uint8_t)(value >> 8); return true;
    case 3: reg.e = (uint8_t)value; reg.d = (uint8_t)(value >> 8); return true;
    case 4: reg.c = (uint8_t)value; reg.b = (uint8_t)(value >> 8); return true;
    case 5: reg.iy = (uint16_t)value; return true;
    case 6:
        reg.l = (uint8_t)value; reg.h = (uint8_t)(value >> 8);
        reg.e = (uint8_t)(value >> 16); reg.d = (uint8_t)(value >> 24);
        return true;
    case 7:
        reg.c = (uint8_t)value; reg.b = (uint8_t)(value >> 8);
        reg.iy = (uint16_t)(value >> 16);
        return true;
    default:
        return false;
    }
}

extern "C" uint8_t z80_debug_instruction_length(uint16_t address)
{
    return x80_instruction_length(address);
}

extern "C" const char *z80_debug_disassemble(uint16_t address)
{
    return x80_render_operation(address);
}

void z80_examine(z80_t *cpu, uint16_t address)
{
    // Jumping to a new PC from the front panel also resumes a halted CPU.
    z80_resume(cpu);
    reg.pc = address;
    cpu->registers.pc = address;
    cpu->address_bus = address;
    cpu->cpuStatus = STATUS_MEMORY_READ;
    cpu->data_bus = memory[address];
    update_display_bus(cpu);
}

void z80_examine_next(z80_t *cpu)
{
    cpu->address_bus++;
    cpu->cpuStatus = STATUS_MEMORY_READ;
    cpu->data_bus = memory[cpu->address_bus];
    update_display_bus(cpu);
}

void z80_deposit(z80_t *cpu, uint8_t data)
{
    cpu->data_bus = data;
    cpu->cpuStatus &= ~STATUS_MEMORY_READ;
    cpu->cpuStatus |= STATUS_WRITE_OUTPUT;
    memory[cpu->address_bus] = data;
    update_display_bus(cpu);
}

void z80_deposit_next(z80_t *cpu, uint8_t data)
{
    z80_examine_next(cpu);
    cpu->data_bus = data;
    cpu->cpuStatus &= ~STATUS_MEMORY_READ;
    cpu->cpuStatus |= STATUS_WRITE_OUTPUT;
    memory[cpu->address_bus] = data;
    update_display_bus(cpu);
}

// --- x80 core callbacks ---------------------------------------------------

// 0x64 (mov h,h) is repurposed as a hook opcode by the ntvcm core. On the
// emulated target it is a genuine (no-op) instruction, so report it as a NOP. These
// callbacks are invoked from x80.cxx, so they keep its (C++) linkage.
uint8_t x80_invoke_hook(void)
{
    return OPCODE_NOP;
}

void x80_invoke_halt(void)
{
    if (g_cpu)
        g_cpu->halted = true;
}

// IN d8. Routes built-in console, disk, and sense ports, then delegates all
// other ports to the optional I/O adapter.
void x80_invoke_in(uint8_t port)
{
    static uint8_t character = 0;
    z80_t *cpu = g_cpu;
    cpu->cpuStatus |= STATUS_PORT_INPUT;

    switch (port)
    {
    case 0x00:
        reg.a = 0x00;
        break;
    case 0x01:
        reg.a = cpu->term_in();
        break;
    case 0x08:
        reg.a = cpu->disk_controller.disk_status();
        break;
    case 0x09:
        reg.a = cpu->disk_controller.sector();
        break;
    case 0x0a:
        reg.a = cpu->disk_controller.read();
        break;
    case 0x10: // 2SIO port 1, status
        reg.a = 0x2; // transmit buffer empty
        if (!character)
            character = cpu->term_in();
        if (character)
            reg.a |= 0x1;
        break;
    case 0x11: // 2SIO port 1, read
        if (character)
        {
            reg.a = character;
            character = 0;
        }
        else
        {
            reg.a = cpu->term_in();
        }
        break;
    case 0xff: // Front panel switches
        reg.a = cpu->sense();
        break;
    default:
        reg.a = cpu->io_port_in_handler(port);
        break;
    }
}

// OUT d8. Routes built-in console and disk ports, then delegates all other
// ports to the optional I/O adapter.
void x80_invoke_out(uint8_t port)
{
    z80_t *cpu = g_cpu;
    cpu->cpuStatus |= STATUS_PORT_OUTPUT | STATUS_WRITE_OUTPUT;

    switch (port)
    {
    case 0x01:
        cpu->term_out(reg.a);
        break;
    case 0x08:
        cpu->disk_controller.disk_select(reg.a);
        break;
    case 0x09:
        cpu->disk_controller.disk_function(reg.a);
        break;
    case 0x0a:
        cpu->disk_controller.write(reg.a);
        break;
    case 0x10: // 2SIO port 1 control
        break;
    case 0x11: // 2SIO port 1 write
        cpu->term_out(reg.a);
        break;
    default:
        cpu->io_port_out_handler(port, reg.a);
        break;
    }
}

void x80_hard_exit(const char *pcerror, uint8_t arg1, uint8_t arg2)
{
    fprintf(stderr, pcerror, arg1, arg2);
    exit(1);
}
