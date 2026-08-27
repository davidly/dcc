extern "C"
{
#include "z80.h"
#include "memory.h"
#include "interrupt_controller.h"
}

#include "x80.hxx"

#include <cassert>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#if TRACK_Z80_R_REGISTER || TRACK_Z80_MEMPTR
#include "x80.hxx"
#endif

static uint8_t no_input(void) { return 0; }
static void no_output(uint8_t) {}
static uint8_t no_sense(void) { return 0; }
static uint8_t port_input_count;
static uint8_t last_input_port;
static uint8_t port_output_count;
static uint8_t last_output_port;
static uint8_t last_output_data;
static uint8_t no_port_input(uint8_t port)
{
    last_input_port = port;
    return ++port_input_count;
}
static void no_port_output(uint8_t port, uint8_t data)
{
    port_output_count++;
    last_output_port = port;
    last_output_data = data;
}
static void raise_interrupt_on_poll(void *context)
{
    interrupt_controller_raise(*static_cast<interrupt_provider_id_t *>(context));
}

static disk_controller_t no_disk = {
    no_output, no_input, no_output, no_input, no_output, no_input
};

static void reset_at(z80_t *cpu, uint16_t pc, uint16_t sp)
{
    memset(memory, 0, 65536);
    port_input_count = last_input_port = 0;
    port_output_count = last_output_port = last_output_data = 0;
    z80_reset(cpu, no_input, no_output, no_sense, &no_disk,
              no_port_input, no_port_output);
    cpu->registers.sp = sp;
    z80_examine(cpu, pc);
}

int main(void)
{
    static_assert(registers::z80_only, "debugger core test requires Z80-only engine");
    z80_t cpu;

    reset_at(&cpu, 0x0080, 0xeffe);
    memory[0xeffe] = 0x01; // F
    memory[0xefff] = 0x12; // A
    memory[0x0080] = 0xf1; // POP AF
    memory[0x0081] = 0x08; // EX AF,AF'
    memory[0x0082] = 0x01; // LD BC,3456h
    memory[0x0083] = 0x56;
    memory[0x0084] = 0x34;
    memory[0x0085] = 0x11; // LD DE,789ah
    memory[0x0086] = 0x9a;
    memory[0x0087] = 0x78;
    memory[0x0088] = 0x21; // LD HL,bcdeh
    memory[0x0089] = 0xde;
    memory[0x008a] = 0xbc;
    memory[0x008b] = 0xd9; // EXX
    z80_execute_instructions(&cpu, 6);
    z80_debug_registers_t debug_registers{};
    z80_debug_get_registers(&debug_registers);
    assert(debug_registers.af_alt == 0x1201);
    assert(debug_registers.bc_alt == 0x3456);
    assert(debug_registers.de_alt == 0x789a);
    assert(debug_registers.hl_alt == 0xbcde);

    reset_at(&cpu, 0x0100, 0xf000);
    memory[0x0100] = 0xfb; // EI
    memory[0x0101] = 0x00; // NOP
    z80_cycle(&cpu);
    assert(!z80_interrupt(&cpu, 0xff));
    z80_cycle(&cpu);
    assert(z80_interrupt(&cpu, 0xff));
    assert(cpu.registers.pc == 0x0038);
    assert(cpu.registers.sp == 0xeffe);
    assert(memory[0xeffe] == 0x02 && memory[0xefff] == 0x01);
    assert(!cpu.iff);
    uint16_t event_address;
    uint8_t event_data;
    uint8_t event_status;
    assert(z80_take_display_event(&cpu, &event_address, &event_data,
                                  &event_status));
    assert(event_address == 0x0038);
    assert(event_data == memory[0x0038]);
    assert(event_status == 0x05); // interrupt acknowledge with stack access
    assert(!z80_take_display_event(&cpu, &event_address, &event_data,
                                   &event_status));

    reset_at(&cpu, 0x0200, 0xe000);
    memory[0x0200] = 0xfb; // EI
    memory[0x0201] = 0x00; // NOP
    memory[0x0202] = 0x76; // HALT
    z80_execute_instructions(&cpu, 3);
    assert(cpu.halted);
    assert(z80_interrupt(&cpu, 0xff));
    assert(!cpu.halted && cpu.registers.pc == 0x0038);

    reset_at(&cpu, 0x0500, 0xb000);
    memory[0x0038] = 0xc3; // JP 0600h
    memory[0x0039] = 0x00;
    memory[0x003a] = 0x06;
    memory[0x0500] = 0xfb; // EI
    memory[0x0501] = 0x76; // HALT
    memory[0x0502] = 0x00; // resumed NOP
    memory[0x0600] = 0xf5; // PUSH AF
    memory[0x0601] = 0x3a; // LD A,(0700h)
    memory[0x0602] = 0x00;
    memory[0x0603] = 0x07;
    memory[0x0604] = 0x3c; // INC A
    memory[0x0605] = 0x32; // LD (0700h),A
    memory[0x0606] = 0x00;
    memory[0x0607] = 0x07;
    memory[0x0608] = 0xf1; // POP AF
    memory[0x0609] = 0xfb; // EI
    memory[0x060a] = 0xed; // RETI
    memory[0x060b] = 0x4d;
    z80_execute_instructions(&cpu, 2);
    assert(z80_interrupt(&cpu, 0xff));
    z80_execute_instructions(&cpu, 8);
    assert(memory[0x0700] == 1);
    assert(cpu.registers.pc == 0x0502);
    assert(cpu.registers.sp == 0xb000);
    assert((cpu.cpuStatus & 0x04) != 0); // RETI popped the interrupt return address

    reset_at(&cpu, 0x0a00, 0xb000);
    memory[0x0038] = 0xc3; // JP 0a20h
    memory[0x0039] = 0x20;
    memory[0x003a] = 0x0a;
    memory[0x0a00] = 0xfb; // EI
    memory[0x0a01] = 0x76; // HALT
    memory[0x0a02] = 0xf3; // DI
    memory[0x0a03] = 0x3a; // LD A,(0b00h)
    memory[0x0a04] = 0x00;
    memory[0x0a05] = 0x0b;
    memory[0x0a06] = 0xfe; // CP 2
    memory[0x0a07] = 0x02;
    memory[0x0a08] = 0x20; // JR NZ,0a00h
    memory[0x0a09] = 0xf6;
    memory[0x0a0a] = 0x76; // final HALT
    memory[0x0a20] = 0xf5; // PUSH AF
    memory[0x0a21] = 0x3a; // LD A,(0b00h)
    memory[0x0a22] = 0x00;
    memory[0x0a23] = 0x0b;
    memory[0x0a24] = 0x3c; // INC A
    memory[0x0a25] = 0x32; // LD (0b00h),A
    memory[0x0a26] = 0x00;
    memory[0x0a27] = 0x0b;
    memory[0x0a28] = 0xf1; // POP AF
    memory[0x0a29] = 0xfb; // EI
    memory[0x0a2a] = 0xed; // RETI
    memory[0x0a2b] = 0x4d;
    z80_execute_instructions(&cpu, 20);
    assert(cpu.halted && cpu.registers.pc == 0x0a02);
    assert(z80_interrupt(&cpu, 0xff));
    z80_execute_instructions(&cpu, 40);
    assert(memory[0x0b00] == 1);
    assert(cpu.halted && cpu.registers.pc == 0x0a02);
    assert(z80_interrupt(&cpu, 0xff));
    z80_execute_instructions(&cpu, 40);
    assert(memory[0x0b00] == 2);
    assert(cpu.halted && cpu.registers.pc == 0x0a0b);

    reset_at(&cpu, 0x0c00, 0xb000);
    memory[0x0c00] = 0xed; // LDIR
    memory[0x0c01] = 0xb0;
    memory[0x0d00] = 0x11;
    memory[0x0d01] = 0x22;
    memory[0x0d02] = 0x33;
    cpu.registers.b = 0x00;
    cpu.registers.c = 0x03;
    cpu.registers.d = 0x0e;
    cpu.registers.e = 0x00;
    cpu.registers.h = 0x0d;
    cpu.registers.l = 0x00;
    z80_execute_instructions(&cpu, 1);
    assert(memory[0x0e00] == 0x11 && memory[0x0e01] == 0x00);
    assert(cpu.registers.pc == 0x0c00);
    assert(cpu.registers.b == 0x00 && cpu.registers.c == 0x02);
    z80_execute_instructions(&cpu, 2);
    assert(memory[0x0e01] == 0x22 && memory[0x0e02] == 0x33);
    assert(cpu.registers.pc == 0x0c02);

    reset_at(&cpu, 0x0c10, 0xb000);
    memory[0x0c10] = 0xfb; // EI
    memory[0x0c11] = 0xed; // LDIR
    memory[0x0c12] = 0xb0;
    memory[0x0d10] = 0x44;
    memory[0x0d11] = 0x55;
    cpu.registers.b = 0x00;
    cpu.registers.c = 0x02;
    cpu.registers.d = 0x0e;
    cpu.registers.e = 0x10;
    cpu.registers.h = 0x0d;
    cpu.registers.l = 0x10;
    z80_execute_instructions(&cpu, 2);
    assert(memory[0x0e10] == 0x44 && memory[0x0e11] == 0x00);
    assert(cpu.registers.pc == 0x0c11);
    assert(z80_interrupt(&cpu, 0xff));
    assert(cpu.registers.pc == 0x0038 && cpu.registers.sp == 0xaffe);
    assert(memory[0xaffe] == 0x11 && memory[0xafff] == 0x0c);

    reset_at(&cpu, 0x0c20, 0xb000);
    memory[0x0c20] = 0xed; // LDIR overwrites its own ED prefix
    memory[0x0c21] = 0xb0;
    memory[0x0d20] = 0x00; // NOP replaces ED
    memory[0x0d21] = 0x44;
    cpu.registers.b = 0x00;
    cpu.registers.c = 0x02;
    cpu.registers.d = 0x0c;
    cpu.registers.e = 0x20;
    cpu.registers.h = 0x0d;
    cpu.registers.l = 0x20;
    z80_execute_instructions(&cpu, 1);
    assert(memory[0x0c20] == 0x00);
    assert(cpu.registers.pc == 0x0c20);
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x0c21);
    assert(cpu.registers.b == 0x00 && cpu.registers.c == 0x01);

    reset_at(&cpu, 0x0c40, 0xb000);
    memory[0x0c40] = 0xed;
    memory[0x0c41] = 0x7c; // undocumented NEG alias
    cpu.registers.a = 0x01;
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.a == 0xff && (cpu.registers.flags & FLAGS_CARRY));

    reset_at(&cpu, 0x0c50, 0x0d00);
    memory[0x0c50] = 0xed;
    memory[0x0c51] = 0x75; // undocumented RETN alias
    memory[0x0d00] = 0x34;
    memory[0x0d01] = 0x12;
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x1234 && cpu.registers.sp == 0x0d02);

    reset_at(&cpu, 0x0c60, 0xb000);
    memory[0x0c60] = 0xed;
    memory[0x0c61] = 0x73; // LD (0d10h),SP
    memory[0x0c62] = 0x10;
    memory[0x0c63] = 0x0d;
    z80_execute_instructions(&cpu, 1);
    assert(memory[0x0d10] == 0x00 && memory[0x0d11] == 0xb0);

    reset_at(&cpu, 0x0c80, 0xb000);
    memory[0x0c80] = 0xed;
    memory[0x0c81] = 0xb2; // INIR
    cpu.registers.a = 0x5a;
    cpu.registers.b = 2;
    cpu.registers.c = 0x23;
    cpu.registers.h = 0x0d;
    cpu.registers.l = 0x40;
    z80_execute_instructions(&cpu, 1);
    assert(memory[0x0d40] == 1 && memory[0x0d41] == 0);
    assert(cpu.registers.a == 0x5a && cpu.registers.b == 1);
    assert(cpu.registers.hl == 0x0d41 && cpu.registers.pc == 0x0c80);
    assert(port_input_count == 1 && last_input_port == 0x23);
    z80_execute_instructions(&cpu, 1);
    assert(memory[0x0d41] == 2 && cpu.registers.b == 0);
    assert(cpu.registers.pc == 0x0c82);

    reset_at(&cpu, 0x0ca0, 0xb000);
    memory[0x0ca0] = 0xed;
    memory[0x0ca1] = 0xab; // OUTD
    memory[0x0d60] = 0xa5;
    cpu.registers.a = 0x5a;
    cpu.registers.b = 1;
    cpu.registers.c = 0x34;
    cpu.registers.h = 0x0d;
    cpu.registers.l = 0x60;
    z80_execute_instructions(&cpu, 1);
    assert(port_output_count == 1 && last_output_port == 0x34);
    assert(last_output_data == 0xa5 && cpu.registers.a == 0x5a);
    assert(cpu.registers.b == 0 && cpu.registers.hl == 0x0d5f);

#if TRACK_Z80_R_REGISTER
    reset_at(&cpu, 0x0cc0, 0xb000);
    memory[0x0cc0] = 0xed;
    memory[0x0cc1] = 0x44; // NEG: opcode plus ED prefix fetch
    reg.r = 0xfe;
    z80_execute_instructions(&cpu, 1);
    assert(reg.r == 0x80);
#endif

#if TRACK_Z80_MEMPTR
    reset_at(&cpu, 0x0cd0, 0xb000);
    memory[0x0cd0] = 0xcb;
    memory[0x0cd1] = 0x46; // BIT 0,(HL)
    memory[0x2800] = 0x01;
    cpu.registers.hl = 0x2800;
    z80_execute_instructions(&cpu, 1);
    assert((cpu.registers.flags & FLAGS_IF) != 0); // Y from MEMPTR bit 13
    assert((cpu.registers.flags & 0x08) != 0);     // X from MEMPTR bit 11
#endif

    reset_at(&cpu, 0x0700, 0xa000);
    memory[0x0700] = 0xaf; // XRA A sets Z
    memory[0x0701] = 0xc0; // RET NZ, not taken
    z80_execute_instructions(&cpu, 2);
    assert(cpu.registers.pc == 0x0702);
    assert(cpu.registers.sp == 0xa000);
    assert((cpu.cpuStatus & 0x04) == 0);

    reset_at(&cpu, 0x0710, 0xa000);
    memory[0x0710] = 0xaf; // XRA A sets Z
    memory[0x0711] = 0xc4; // CALL NZ,1234h, not taken
    memory[0x0712] = 0x34;
    memory[0x0713] = 0x12;
    z80_execute_instructions(&cpu, 2);
    assert(cpu.registers.pc == 0x0714);
    assert(cpu.registers.sp == 0xa000);
    assert((cpu.cpuStatus & 0x04) == 0);

    reset_at(&cpu, 0x0720, 0xa000);
    memory[0x0720] = 0xcd; // CALL 1234h
    memory[0x0721] = 0x34;
    memory[0x0722] = 0x12;
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x1234);
    assert(cpu.registers.sp == 0x9ffe);
    assert((cpu.cpuStatus & 0x04) != 0);

    reset_at(&cpu, 0x0740, 0xa000);
    memory[0x0740] = 0xdd; // LD IX,1234h
    memory[0x0741] = 0x21;
    memory[0x0742] = 0x34;
    memory[0x0743] = 0x12;
    memory[0x0744] = 0xdd; // PUSH IX
    memory[0x0745] = 0xe5;
    memory[0x0746] = 0xdd; // POP IX
    memory[0x0747] = 0xe1;
    z80_execute_instructions(&cpu, 3);
    assert(cpu.registers.pc == 0x0748);
    assert(cpu.registers.sp == 0xa000);
    assert(memory[0x9ffe] == 0x34 && memory[0x9fff] == 0x12);
    assert((cpu.cpuStatus & 0x04) != 0);

    reset_at(&cpu, 0x0760, 0xa000);
    memory[0x0760] = 0xed; // LD (2000h),SP
    memory[0x0761] = 0x73;
    memory[0x0762] = 0x00;
    memory[0x0763] = 0x20;
    memory[0x0764] = 0x31; // LD SP,9000h
    memory[0x0765] = 0x00;
    memory[0x0766] = 0x90;
    memory[0x0767] = 0xed; // LD SP,(2000h)
    memory[0x0768] = 0x7b;
    memory[0x0769] = 0x00;
    memory[0x076a] = 0x20;
    z80_execute_instructions(&cpu, 3);
    assert(memory[0x2000] == 0x00 && memory[0x2001] == 0xa0);
    assert(cpu.registers.sp == 0xa000);

    reset_at(&cpu, 0x0780, 0xa000);
    memory[0x0780] = 0xdd; // LD IX,8123h
    memory[0x0781] = 0x21;
    memory[0x0782] = 0x23;
    memory[0x0783] = 0x81;
    memory[0x0784] = 0xdd; // LD SP,IX
    memory[0x0785] = 0xf9;
    z80_execute_instructions(&cpu, 2);
    assert(cpu.registers.pc == 0x0786);
    assert(cpu.registers.sp == 0x8123);

    // DD/FD-prefixed opcodes that don't reference H, L, or (HL) execute
    // exactly as if unprefixed (the prefix is simply wasted). This was
    // previously unimplemented and aborted the host process; regression
    // covers the general fallback plus two related pre-existing mask bugs
    // that were found and fixed alongside it.
    reset_at(&cpu, 0x0790, 0xa000);
    memory[0x0790] = 0xfd; // FD 15: DD/FD-prefixed DEC D (no H/L reference)
    memory[0x0791] = 0x15;
    cpu.registers.de = 0x0500;
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x0792);
    assert((cpu.registers.de >> 8) == 0x04);

    // LXI B (0x01), PUSH BC, POP DE: none reference H/L
    reset_at(&cpu, 0x07a0, 0xa000);
    memory[0x07a0] = 0xdd; memory[0x07a1] = 0x01; memory[0x07a2] = 0x34; memory[0x07a3] = 0x12; // DD 01 34 12: LXI B,1234h
    memory[0x07a4] = 0xdd; memory[0x07a5] = 0xc5; // DD C5: PUSH BC
    memory[0x07a6] = 0xdd; memory[0x07a7] = 0xd1; // DD D1: POP DE
    z80_execute_instructions(&cpu, 3);
    assert(cpu.registers.pc == 0x07a8);
    assert(cpu.registers.bc == 0x1234);
    assert(cpu.registers.de == 0x1234);
    assert(cpu.registers.sp == 0xa000);

    // Conditional jump
    reset_at(&cpu, 0x07b0, 0xa000);
    cpu.registers.af = 0x0040; // Z flag set
    memory[0x07b0] = 0xfd; memory[0x07b1] = 0xca; memory[0x07b2] = 0x00; memory[0x07b3] = 0x08; // FD CA 0000 0800: JZ 0800h
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x0800);

    // CALL/RET
    reset_at(&cpu, 0x0810, 0xa000);
    memory[0x0810] = 0xdd; memory[0x0811] = 0xcd; memory[0x0812] = 0x00; memory[0x0813] = 0x09; // DD CD 0000 0900: CALL 0900h
    memory[0x0900] = 0xfd; memory[0x0901] = 0xc9; // FD C9: RET
    z80_execute_instructions(&cpu, 2);
    assert(cpu.registers.pc == 0x0814);
    assert(cpu.registers.sp == 0xa000);

    // ADI: previously misdecoded as "ld r,(i+d)" because the 0x46==(op2&0x47)
    // mask omitted bit 7, wrongly also matching 0xc6/ce/d6/de/e6/ee/f6/fe.
    reset_at(&cpu, 0x0820, 0xa000);
    cpu.registers.a = 0x05;
    memory[0x0820] = 0xdd; memory[0x0821] = 0xc6; memory[0x0822] = 0x03; // DD C6 03: ADI 3
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x0823);
    assert(cpu.registers.a == 0x08);

    // ADD B: previously misdecoded as math on IXH/IXL because the
    // 0x80==(op2&0xc2) mask omitted bit 2, wrongly also matching register
    // B/C forms (which don't reference H/L at all).
    reset_at(&cpu, 0x0850, 0xa000);
    cpu.registers.a = 0x05;
    cpu.registers.b = 0x03;
    memory[0x0850] = 0xdd; memory[0x0851] = 0x80; // DD 80: ADD A,B
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x0852);
    assert(cpu.registers.a == 0x08);
    assert(cpu.registers.b == 0x03); // unaffected

    // JR and EXX: Z80-only opcodes with no H/L involvement are forwarded to
    // the existing z80_emulate handler rather than reimplemented here.
    reset_at(&cpu, 0x0830, 0xa000);
    memory[0x0830] = 0xfd; memory[0x0831] = 0x18; memory[0x0832] = 0x05; // FD 18 05: JR +5
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x0838);

    reset_at(&cpu, 0x0860, 0xa000);
    cpu.registers.bc = 0x1122;
    memory[0x0860] = 0xdd; memory[0x0861] = 0xd9; // DD D9: EXX
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x0862);
    assert(cpu.registers.bc != 0x1122); // swapped with the (zeroed) shadow set

    // NEG via ED: ED-prefixed instructions always ignore a preceding DD/FD.
    reset_at(&cpu, 0x0840, 0xa000);
    memory[0x0840] = 0xdd; memory[0x0841] = 0xed; memory[0x0842] = 0x44; // DD ED 44: NEG
    cpu.registers.a = 0x01;
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x0843);
    assert(cpu.registers.a == 0xff);

    reset_at(&cpu, 0x0280, 0xe000);
    memory[0x0280] = 0xfb; // EI
    memory[0x0281] = 0x76; // HALT
    z80_execute_instructions(&cpu, 2);
    assert(cpu.halted);
    assert(z80_interrupt(&cpu, 0xff));
    assert(!cpu.halted && cpu.registers.pc == 0x0038);

    reset_at(&cpu, 0x0300, 0xd000);
    memory[0x0300] = 0xed; // IM 2
    memory[0x0301] = 0x5e;
    memory[0x0302] = 0x3e; // LD A,12h
    memory[0x0303] = 0x12;
    memory[0x0304] = 0xed; // LD I,A
    memory[0x0305] = 0x47;
    memory[0x0306] = 0xfb; // EI
    memory[0x0307] = 0x00; // NOP
    memory[0x1234] = 0x78;
    memory[0x1235] = 0x56;
    z80_execute_instructions(&cpu, 4);
    z80_execute_instructions(&cpu, 1);
    assert(cpu.interrupt_mode == 2);
    assert(z80_interrupt(&cpu, 0x34));
    assert(cpu.registers.pc == 0x5678);

    reset_at(&cpu, 0x0400, 0xc000);
    memory[0x0038] = 0xc9; // RET
    memory[0x0400] = 0xfb; // EI
    memory[0x0401] = 0x00; // NOP
    interrupt_controller_init();
    interrupt_provider_id_t polled_provider;
    const interrupt_provider_config_t polled_config = {
        0xff, raise_interrupt_on_poll, &polled_provider};
    assert(interrupt_controller_register(&polled_config, &polled_provider));
    z80_execute_instructions(&cpu, 2);
    assert(interrupt_controller_service(&cpu));
    assert(cpu.registers.pc == 0x0038);

    interrupt_provider_id_t high_provider;
    interrupt_provider_id_t low_provider;
    const interrupt_provider_config_t high_config = {0xcf, NULL, NULL};
    const interrupt_provider_config_t low_config = {0xff, NULL, NULL};
    interrupt_controller_init();
    assert(interrupt_controller_register(&high_config, &high_provider));
    assert(interrupt_controller_register(&low_config, &low_provider));
    interrupt_controller_raise(low_provider);
    interrupt_controller_raise(high_provider);

    reset_at(&cpu, 0x0800, 0xa000);
    memory[0x0800] = 0xfb; // EI
    memory[0x0801] = 0x00; // NOP
    z80_execute_instructions(&cpu, 2);
    assert(interrupt_controller_service(&cpu));
    assert(cpu.registers.pc == 0x0008); // high-priority provider's RST 1

    reset_at(&cpu, 0x0900, 0x9000);
    memory[0x0900] = 0xfb; // EI
    memory[0x0901] = 0x00; // NOP
    z80_execute_instructions(&cpu, 2);
    assert(interrupt_controller_service(&cpu));
    assert(cpu.registers.pc == 0x0038); // lower-priority request remained pending

    // DD 76 / FD 76 (prefixed HALT) must still be routed to the
    // not-implemented abort path (z80_ni -> x80_hard_exit -> exit(1)). On
    // real Z80 hardware, DD 76 / FD 76 execute as plain, undisturbed HALT
    // (0x76 is the reserved "hole" in the LD r,(HL)/LD (HL),r opcode grid,
    // not an actual (HL) reference); this emulator cannot correctly stop its
    // instruction-fetch loop mid-batch from within z80_emulate (a separate
    // function from the loop that owns that control flow), so it
    // deliberately treats prefixed HALT as an unimplemented, disclosed
    // limitation rather than approximate it. 0x76 incidentally satisfies the
    // "ld r,(i+d)" mask earlier in the 0xdd/0xfd dispatch chain purely by
    // bit-pattern coincidence (0x76 & 0xc7 == 0x46), so this guards against
    // silently reintroducing that dispatch-order bug, where it would be
    // misinterpreted as a completely different instruction instead of ever
    // reaching the exclusion check.
#ifndef _WIN32
    auto assert_prefixed_op_aborts = [](uint8_t prefix, uint8_t op2)
    {
        pid_t child = fork();
        assert(child >= 0);
        if (0 == child)
        {
            freopen("/dev/null", "w", stderr);
            z80_t cpu;
            memset(memory, 0, 65536);
            z80_reset(&cpu, no_input, no_output, no_sense, &no_disk,
                      no_port_input, no_port_output);
            cpu.registers.sp = 0xf000;
            z80_examine(&cpu, 0x0100);
            memory[0x0100] = prefix;
            memory[0x0101] = op2;
            z80_execute_instructions(&cpu, 1);
            _exit(0); // only reached if the abort did NOT happen -- a failure
        }
        int status = 0;
        assert(child == waitpid(child, &status, 0));
        assert(WIFEXITED(status) && 1 == WEXITSTATUS(status));
    };

    assert_prefixed_op_aborts(0xdd, 0x76); // DD 76
    assert_prefixed_op_aborts(0xfd, 0x76); // FD 76
#endif

    // DD 64 / FD 64 (unprefixed 0x64 == MOV H,H, this codebase's own
    // debug-hook opcode) must NOT be treated as a hook or aborted: on real
    // Z80 hardware this is the genuine, if undocumented, self-assignment
    // LD IXH,IXH / LD IYH,IYH, executed by the same pre-existing
    // register-substitution logic that already handles other IXH/IXL/IYH/IYL
    // forms. The debug-hook convention is deliberately unprefixed-opcode-only
    // and has no bearing on the prefixed form. (IX/IY aren't exposed via the
    // public registers_t, so verify indirectly: LD IX,nnnn, execute DD 64,
    // then LD (addr),IX and confirm the stored value is unchanged.)
    reset_at(&cpu, 0x0870, 0xa000);
    memory[0x0870] = 0xdd; memory[0x0871] = 0x21; memory[0x0872] = 0x34; memory[0x0873] = 0x12; // DD 21 3412: LD IX,1234h
    memory[0x0874] = 0xdd; memory[0x0875] = 0x64;                                               // DD 64: LD IXH,IXH
    memory[0x0876] = 0xdd; memory[0x0877] = 0x22; memory[0x0878] = 0x00; memory[0x0879] = 0x50; // DD 22 0050: LD (5000h),IX
    z80_execute_instructions(&cpu, 3);
    assert(cpu.registers.pc == 0x087a);
    assert(memory[0x5000] == 0x34 && memory[0x5001] == 0x12); // IX unchanged

    reset_at(&cpu, 0x0880, 0xa000);
    memory[0x0880] = 0xfd; memory[0x0881] = 0x21; memory[0x0882] = 0x78; memory[0x0883] = 0x56; // FD 21 7856: LD IY,5678h
    memory[0x0884] = 0xfd; memory[0x0885] = 0x64;                                               // FD 64: LD IYH,IYH
    memory[0x0886] = 0xfd; memory[0x0887] = 0x22; memory[0x0888] = 0x02; memory[0x0889] = 0x50; // FD 22 0250: LD (5002h),IY
    z80_execute_instructions(&cpu, 3);
    assert(cpu.registers.pc == 0x088a);
    assert(memory[0x5002] == 0x78 && memory[0x5003] == 0x56); // IY unchanged

    puts("x80 interrupt tests passed");
    return 0;
}