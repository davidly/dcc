// Z80-only emulator for dcc_debug_host.
// Derived from ntvcm x80.cxx by David Lee, CC0 1.0 Universal.
// Source lineage includes ntvcm through commit dce7246 (2026-08-09),
// specialized to Z80 and adapted for the debugger hardware API.
// This debugger-local copy intentionally contains no Intel 8080 mode.
// Symbols that start with z80_ are Z80-specific helpers.
// Validated 100% pass for Z80 with zexall.com, zexdoc.com, and cputest.com in Z80 mode.

#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <vector>
#include <cstring>
#include <bitset>
#include <djl_os.hxx>
#include <djltrace.hxx>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define X80_HOT_CODE IRAM_ATTR
#else
#define X80_HOT_CODE
#endif

using namespace std;

#include "x80.hxx"

static_assert(DEBUGGER_X80_Z80_ONLY == 1, "dcc_debug_host requires its Z80-only core");

// memory[] is defined in memory.c and shared with the rest of the debugger
// hardware (ROM loaders and disk controller). x80.hxx declares it
// extern, so the core writes into the same 64KB array everyone else uses.
registers reg;
uint8_t x80_last_io_status = X80_IO_NONE;
uint16_t x80_last_sp_before = 0;
uint8_t x80_last_opcode = OPCODE_NOP;
static const char * reg_strings[ 8 ] = { "b", "c", "d", "e", "h", "l", "m", "a" };
static const char * rp_strings[ 4 ] = { "bc", "de", "hl", "sp" };
static const char * z80_math_strings[ 8 ] = { "add", "adc", "sub", "sbb", "and", "xor", "or", "cp" };
static const char * z80_rotate_strings[ 8 ] = { "rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl" };
static uint8_t g_State = 0;
const uint8_t stateTraceInstructions = 1;
const uint8_t stateEndEmulation = 2;
void x80_trace_instructions( bool t ) { if ( t ) g_State |= stateTraceInstructions; else g_State &= ~stateTraceInstructions; }
void x80_end_emulation() { g_State |= stateEndEmulation; }

enum z80_value_source { vs_register, vs_memory, vs_indexed }; // this impacts how Z80 undocumented Y and X flags are updated
const uint8_t cyclesnt = 6;  // cycles not taken when a conditional call, jump, or return isn't taken

// instructions starting with '*' are Z80-specific, generally multi-byte, and handled separately.
// instructions listed here are the overlap with 8080 but with the Z80 naming.
static const char z80_instructions[ 256 ][ 16 ] =
{
    /*00*/ "nop",        "ld bc, d16", "ld (bc), a",   "inc bc",      "inc b",        "dec b",      "ld b, d8",    "rlca",
    /*08*/ "*'",         "add hl, bc", "ld a, (bc)",   "dec bc",      "inc c",        "dec c",      "ld c, d8",    "rrca",
    /*10*/ "*",          "ld de, d16", "ld (de), a",   "inc de",      "inc d",        "dec d",      "ld d, d8",    "rla",
    /*18*/ "*",          "add hl, de", "ld a, (de)",   "dec de",      "inc e",        "dec e",      "ld e, d8",    "rra",
    /*20*/ "*",          "ld hl, d16", "ld (a16), hl", "inc hl",      "inc h",        "dec h",      "ld h, d8",    "daa",
    /*28*/ "*",          "add hl, hl", "ld hl, (a16)", "dec hl",      "inc l",        "dec l",      "ld l, d8",    "cpl",
    /*30*/ "*",          "ld sp, d16", "ld (a16), a",  "inc sp",      "inc (hl)",     "dec (hl)",   "ld (hl), d8", "scf",
    /*38*/ "*",          "add hl, sp", "ld a, (a16)",  "dec sp",      "inc a",        "dec a",      "ld a, d8",    "ccf",
    /*40*/ "ld b, b",    "ld b, c",    "ld b, d",      "ld b, e",     "ld b, h",      "ld b, l",    "ld b, (hl)",  "ld b, a",
    /*48*/ "ld c, b",    "ld c, c",    "ld c, d",      "ld c, e",     "ld c, h",      "ld c, l",    "ld c, (hl)",  "ld c, a",
    /*50*/ "ld d, b",    "ld d, c",    "ld d, d",      "ld d, e",     "ld d, h",      "ld d, l",    "ld d, (hl)",  "ld d, a",
    /*58*/ "ld e, b",    "ld e, c",    "ld e, d",      "ld e, e",     "ld e, h",      "ld e, l",    "ld e, (hl)",  "ld e, a",
    /*60*/ "ld h, b",    "ld h, c",    "ld h, d",      "ld h, e",     "(hook)",       "ld h, l",    "ld h, (hl)",  "ld h, a",
    /*68*/ "ld l, b",    "ld l, c",    "ld l, d",      "ld l, e",     "ld l, h",      "ld l, l",    "ld l, (hl)",  "ld l, a",
    /*70*/ "ld (hl), b", "ld (hl), c", "ld (hl), d",   "ld (hl), e",  "ld (hl), h",   "ld (hl), l", "halt",        "ld (hl), a",
    /*78*/ "ld a, b",    "ld a, c",    "ld a, d",      "ld a, e",     "ld a, h",      "ld a, l",    "ld a, (hl)",  "ld a, a",
    /*80*/ "add a, b",   "add a, c",   "add a, d",     "add a, e",    "add a, h",     "add a, l",   "add a, (hl)", "add a, a",
    /*88*/ "adc a, b",   "adc a, c",   "adc a, d",     "adc a, e",    "adc a, h",     "adc a, l",   "adc a, (hl)", "adc a, a",
    /*90*/ "sub b",      "sub c",      "sub d",        "sub e",       "sub h",        "sub l",      "sub (hl)",    "sub a",
    /*98*/ "sbc a, b",   "sbc a, c",   "sbc a, d",     "sbc a, e",    "sbc a, h",     "sbc a, l",   "sbc a, (hl)", "sbc a, a",
    /*a0*/ "and b",      "and c",      "and d",        "and e",       "and h",        "and l",      "and (hl)",    "and a",
    /*a8*/ "xor b",      "xor c",      "xor d",        "xor e",       "xor h",        "xor l",      "xor (hl)",    "xor a",
    /*b0*/ "or b",       "or c",       "or d",         "or e",        "or h",         "or l",       "or (hl)",     "or a",
    /*b8*/ "cp b",       "cp c",       "cp d",         "cp e",        "cp h",         "cp l",       "cp (hl)",     "cp a",
    /*c0*/ "ret nz",     "pop bc",     "jp nz, a16",   "jp a16",      "call nz, a16", "push bc",    "add a, d8",   "rst 0",
    /*c8*/ "ret z",      "ret",        "jp z, a16",    "*",           "call z, a16",  "call a16",   "adc a, d8",   "rst 1",
    /*d0*/ "ret nc",     "pop de",     "jp nc, a16",   "out (d8), a", "call nc, a16", "push de",    "sub d8",      "rst 2",
    /*d8*/ "ret c",      "*",          "jp c, a16",    "in a, (d8)",  "call c, a16",  "*",          "sbc d8",      "rst 3",
    /*e0*/ "ret po",     "pop hl",     "jp po, a16",   "ex (sp), hl", "call po, a16", "push hl",    "and d8",      "rst 4",
    /*e8*/ "ret pe",     "jp (hl)",    "jp pe, a16",   "ex de, hl",   "call pe, a16", "*",          "xor d8",      "rst 5",
    /*f0*/ "ret p",      "pop af",     "jp p, a16",    "di",          "call p, a16",  "push af",    "or d8",       "rst 6",
    /*f8*/ "ret m",      "ld sp, hl",  "jp m, a16",    "ei",          "call m, a16",  "*",          "cp d8",       "rst 7",
};

typedef uint8_t acycles_t[ 256 ];
static const acycles_t z80_cycles =
{
    /*00*/  4, 10,  7,  6,  4,  4,  7,  4,    4, 11,  7,  6,  4,  4,  7,  4,
    /*10*/  0, 10,  7,  6,  4,  4,  7,  4,    0, 11,  7,  6,  4,  4,  7,  4,
    /*20*/  0, 10, 16,  6,  4,  4,  7,  4,    0, 11, 20,  6,  4,  4,  7,  4,
    /*30*/  0, 10, 13,  6, 11, 11, 10,  4,    0, 11, 13,  6,  4,  4,  7,  4,
    /*40*/  4,  4,  4,  4,  4,  4,  7,  4,    4,  4,  4,  4,  4,  4,  7,  4,
    /*50*/  4,  4,  4,  4,  4,  4,  7,  4,    4,  4,  4,  4,  4,  4,  7,  4,
    /*60*/  4,  4,  4,  4,  0,  4,  7,  4,    4,  4,  4,  4,  4,  4,  7,  4,
    /*70*/  7,  7,  7,  7,  7,  7,  4,  7,    4,  4,  4,  4,  4,  4,  7,  4,
    /*80*/  4,  4,  4,  4,  4,  4,  7,  4,    4,  4,  4,  4,  4,  4,  7,  4,
    /*90*/  4,  4,  4,  4,  4,  4,  7,  4,    4,  4,  4,  4,  4,  4,  7,  4,
    /*a0*/  4,  4,  4,  4,  4,  4,  7,  4,    4,  4,  4,  4,  4,  4,  7,  4,
    /*b0*/  4,  4,  4,  4,  4,  4,  7,  4,    4,  4,  4,  4,  4,  4,  7,  4,
    /*c0*/ 11, 10, 10, 10, 17, 11,  7, 11,   11, 10, 10,  0, 17, 17,  7, 11,
    /*d0*/ 11, 10, 10, 11, 17, 11,  7, 11,   11,  0, 10, 11, 17,  0,  7, 11,
    /*e0*/ 11, 10, 10, 19, 17, 11,  7, 11,   11,  4, 10,  4, 17,  0,  7, 11,
    /*f0*/ 11, 10, 10,  4, 17, 11,  7, 11,   11,  5, 10,  4, 17,  0,  7, 11,
};

static uint8_t pcbyte( uint16_t & pc ) { return memory[ pc++ ]; }

#ifdef TARGET_BIG_ENDIAN
static uint16_t mword( uint16_t offset ) { return flip_endian16( * ( (uint16_t *) & memory[ offset ] ) ); }
static void setmword( uint16_t offset, uint16_t value ) { * (uint16_t *) & memory[ offset ] = flip_endian16( value ); }
#else
static uint16_t mword( uint16_t offset ) { return * ( (uint16_t *) & memory[ offset ] ); }
static void setmword( uint16_t offset, uint16_t value ) { * (uint16_t *) & memory[ offset ] = value; }
#endif

static uint16_t pcword( uint16_t & pc ) { uint16_t r = mword( pc ); pc += 2; return r; }
static void pushword( uint16_t & sp, uint16_t val ) { sp -= 2; setmword( sp, val ); }
static uint16_t popword( uint16_t & sp ) { uint16_t val = mword( sp ); sp += 2; return val; }

void set_parity( uint8_t x ) { reg.fParityEven_Overflow = is_parity_even8( x ); }

void set_sign_zero( uint8_t x )
{
    reg.fSign = ( 0 != ( 0x80 & x ) );
    reg.fZero = ( 0 == x );
} //set_sign_zero

void set_sign_zero_parity( uint8_t x )
{
    set_sign_zero( x );
    set_parity( x );
} //set_sign_zero_parity

void z80_set_sign_zero_16( uint16_t x )
{
    reg.fSign = ( 0 != ( 0x8000 & x ) );
    reg.fZero = ( 0 == x );
} //z80_set_sign_zero_16

uint8_t op_inc( uint8_t x )
{
    x++;
    reg.fAuxCarry = ( 0 == ( x & 0xf ) );
    set_sign_zero( x );

    reg.fParityEven_Overflow = ( x == 0x80 );
    reg.fWasSubtract = false;
    reg.z80_assignYX( x );
    return x;
} //op_inc

uint8_t op_dec( uint8_t x )
{
    uint8_t result = x - 1;
    set_sign_zero( result );

    reg.fParityEven_Overflow = ( x == 0x80 );
    reg.fWasSubtract = true;
    reg.fAuxCarry = ( 0xf == ( result & 0xf ) );
    reg.z80_assignYX( result );

    return result;
} //op_dec

force_inlined void op_add( uint8_t x, bool carry = false )
{
    uint16_t carry_int = carry ? 1 : 0;
    uint16_t r16 = (uint16_t) reg.a + (uint16_t) x + carry_int;
    uint8_t r8 = r16 & 0xff;
    reg.fCarry = ( 0 != ( r16 & 0x0100 ) );

    // low nibble add + carry overflows to high nibble

    reg.fAuxCarry = ( 0 != ( ( ( 0xf & reg.a ) + ( 0xf & x ) + carry_int ) & 0x10 ) );
    set_sign_zero( r8 );

    // if ( not ( one of lhs and rhs are negative ) ) and ( one of lhs and result are negative )

    reg.fParityEven_Overflow = ( ! ( ( reg.a ^ x ) & 0x80 ) ) && ( ( reg.a ^ r8 ) & 0x80 );
    reg.fWasSubtract = false;
    reg.z80_assignYX( r8 );

    reg.a = r8;
} //op_add

void op_adc( uint8_t x )
{
    op_add( x, reg.fCarry );
} //op_adc

force_inlined uint8_t op_sub( uint8_t x, bool borrow = false )
{
    // com == ones-complement
    uint8_t com_x = ~x;
    uint8_t borrow_int = borrow ? 0 : 1;
    uint16_t res16 =  (uint16_t) reg.a + (uint16_t) com_x + (uint16_t) borrow_int;
    uint8_t res8 = res16 & 0xff;
    reg.fCarry = ( 0 == ( res16 & 0x100 ) );
    set_sign_zero( res8 );
    reg.fAuxCarry = ( 0 != ( ( ( reg.a & 0xf ) + ( com_x & 0xf ) + borrow_int ) & 0x10 ) );

    // if not ( ( one of lhs and com_x are negative ) and ( one of lhs and result are negative ) )
    reg.fParityEven_Overflow = ! ( ( reg.a ^ com_x ) & 0x80 ) && ( ( reg.a ^ res8 ) & 0x80 );
    reg.fWasSubtract = true;
    reg.fAuxCarry = !reg.fAuxCarry;
    reg.z80_assignYX( res8 );

    // op_sub is the only op_X that doesn't update reg.a because it's also used for op_cmp
    return res8;
} //op_sub

force_inlined void op_sbb( uint8_t x )
{
    reg.a = op_sub( x, reg.fCarry );
} //op_sbb

force_inlined void op_cmp( uint8_t x )
{
    op_sub( x, false );
    reg.z80_assignYX( x ); // done on operand, not the result or reg.a
} //op_cmp

force_inlined void op_ana( uint8_t x )
{
    reg.fCarry = false;
    reg.a &= x;
    set_sign_zero_parity( reg.a );
    reg.fWasSubtract = false;
    reg.fAuxCarry = true;
    reg.z80_assignYX( reg.a );
} //op_ana

force_inlined void op_ora( uint8_t x )
{
    reg.a |= x;
    reg.fAuxCarry = false;
    reg.fCarry = false;
    set_sign_zero_parity( reg.a );

    reg.fWasSubtract = false;
    reg.z80_assignYX( reg.a );
} //op_ora

force_inlined void op_xra( uint8_t x )
{
    reg.a ^= x;
    reg.fAuxCarry = false;
    reg.fCarry = false;
    set_sign_zero_parity( reg.a );

    reg.fWasSubtract = false;
    reg.z80_assignYX( reg.a );
} //op_xra

void op_math( uint8_t opcode, uint8_t src )
{
    uint8_t math = ( opcode >> 3 ) & 7;
    assert( math <= 7 );
    if ( 7 == math ) op_cmp( src );         // in order of usage for performance
    else if ( 6 == math ) op_ora( src );
    else if ( 4 == math ) op_ana( src );
    else if ( 5 == math ) op_xra( src );
    else if ( 0 == math ) op_add( src );
    else if ( 2 == math ) reg.a = op_sub( src ); // sub doesn't update reg.a
    else if ( 1 == math ) op_adc( src );
    else op_sbb( src ); // 3
} //op_math

force_inlined void op_dad( uint16_t x )
{
    // add x to H and set Carry if warranted

    uint16_t oldH = reg.H();
    uint32_t result = (uint32_t) oldH + (uint32_t) x;
    reg.fCarry = ( 0 != ( 0x10000 & result ) );

    uint32_t auxResult = ( reg.H() & 0xfff ) + ( x & 0xfff );
    reg.fAuxCarry = ( 0 != ( auxResult & 0xf000 ) );
    reg.fWasSubtract = false;
    reg.z80_assignYX( (uint8_t) ( result >> 8 ) );
    reg.z80_set_memptr( oldH + 1 );

    reg.SetH( (uint16_t) ( result & 0xffff ) );
} //op_dad

void op_cma()
{
    reg.a = ~reg.a;
    reg.fAuxCarry = true;
    reg.fWasSubtract = true;
    reg.z80_assignYX( reg.a );
} //op_cma

void op_cmc()
{
    reg.fCarry = !reg.fCarry;
    reg.fWasSubtract = false;
    reg.fAuxCarry = !reg.fCarry; // some docs say !reg.fAuxCarry
    reg.z80_assignYX( reg.a );
} //op_cmc

not_inlined void op_daa()
{
    // This BCD add logic is pulled from Sean Young's Z80 documentation.
        uint8_t diff = 0x66;
        uint8_t hn = ( ( reg.a >> 4 ) & 0xf );
        uint8_t ln = ( reg.a & 0xf );

        if ( reg.fCarry )
        {
            if ( !reg.fAuxCarry && ln <= 9 )
                diff = 0x60;
        }
        else
        {
            if ( hn <= 9 && !reg.fAuxCarry && ln <= 9 )
                diff = 0;
            else if ( ( hn <= 9 && reg.fAuxCarry && ln <= 9 ) || ( hn <= 8 && ln > 9 ) )
                diff = 6;
            else if ( hn > 9 && !reg.fAuxCarry && ln <= 9 )
                diff = 0x60;
        }

        bool newCarry = reg.fCarry;
        if ( !reg.fCarry )
        {
            if ( ( hn <= 9 && ln <= 9 ) || ( hn <= 8 && ln > 9 ) )
                newCarry = false;
            else if ( ( hn >= 9 && ln > 9 ) || ( hn > 9 && ln <= 9 ) )
                newCarry = true;
        }

        bool newAuxCarry = reg.fAuxCarry;
        if ( reg.fWasSubtract )
        {
            if ( !reg.fAuxCarry )
                newAuxCarry = false;
            else
                newAuxCarry = ( ln < 6 );
        }
        else
            newAuxCarry = ( ln > 9 );

        if ( reg.fWasSubtract )
            reg.a -= diff;
        else
            reg.a += diff;

        set_sign_zero_parity( reg.a );
        reg.z80_assignYX( reg.a );
        reg.fCarry = newCarry;
        reg.fAuxCarry = newAuxCarry;
} //op_daa

uint8_t * dst_address_rm( uint8_t rm )
{
    assert( rm <= 7 );
    if ( 6 != rm )
        return reg.regOffset( rm );

    return & memory[ reg.H() ];
} //dst_address_rm

uint8_t * dst_address( uint8_t op )
{
    uint8_t rm = 7 & ( op >> 3 );
    return dst_address_rm( rm );
} //dst_address

uint8_t src_value_rm( uint8_t rm )
{
    if ( 6 != rm )
        return * ( reg.regOffset( rm ) );
    return memory[ reg.H() ];
} //src_value_rm

uint8_t src_value( uint8_t op )
{
    uint8_t rm = 0x7 & op;
    return src_value_rm( rm );
} //src_value

void z80_ni( uint8_t op, uint8_t op2 )
{
    x80_hard_exit( "bugbug: not-implemented z80 instruction: %#x, next byte is %#x\n", op, op2 );
} //z80_ni

bool check_conditional( uint8_t op ); // defined below; needed by z80_execute_unprefixed
uint16_t z80_emulate( uint8_t op, uint16_t & pc, uint16_t & sp ); // defined below; needed by z80_execute_unprefixed

// On real Z80 hardware, a DD/FD prefix has no effect on the operands or
// semantics of any instruction that doesn't reference H, L, or (HL): the
// following opcode executes with its normal instruction semantics. The
// DD/FD prefix is nevertheless consumed as an additional M1/prefix cycle,
// including its own effect on the refresh register (see z80_bump_r()
// below). Every instruction that *does* reference H/L/(HL) is already
// redirected to IX/IY (or IXH/IXL/(IX+d)) by the explicit cases in the
// 0xdd/0xfd handler below; this function covers the rest of the opcode
// space so that code compiled or assembled with an incidental DD/FD prefix
// -- for example a
// timing-padded DEC D (0xFD 0x15), seen in a real CP/M PIP.COM -- executes
// correctly instead of aborting the emulator. It reuses the same low-level
// helpers as the primary 8080-compatible dispatch to avoid duplicating
// instruction semantics. HALT (0x76) is excluded before this function is
// ever called (see the 0xdd/0xfd handler above) and still routes to z80_ni:
// on real hardware DD 76 / FD 76 is plain, undisturbed HALT, but correctly
// stopping this emulator's instruction-fetch loop immediately (matching the
// real, unprefixed 0x76 case) would require signaling back across a
// function-call boundary that doesn't exist today, so it is deliberately
// left as a disclosed, not-implemented limitation rather than approximated.
// The debug-hook opcode (0x64, unprefixed MOV H,H in this codebase's own
// convention) is never seen here either, but for an entirely different
// reason: its prefixed form (DD 64 / FD 64) is genuinely, if
// undocumentedly, LD IXH,IXH / LD IYH,IYH on real hardware and is already
// correctly handled by the pre-existing register-substitution case above --
// the debug-hook convention is deliberately unprefixed-opcode-only and has
// no bearing on that form.
uint16_t z80_execute_unprefixed( uint8_t op, uint8_t op2, uint16_t & pc, uint16_t & sp )
{
    uint16_t cycles = z80_cycles[ op2 ];
    switch ( op2 )
    {
        case 0x00: break; // nop
        case 0x01: case 0x11: reg.setRpValueFromOp( op2, pcword( pc ) ); break; // lxi bc/de
        case 0x31: sp = pcword( pc ); break; // lxi sp, d16
        case 0x02: memory[ reg.B() ] = reg.a; reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) | ( ( reg.B() + 1 ) & 0xff ) ); break; // stax b
        case 0x03: case 0x13: { uint8_t rp = ( op2 >> 4 ) & 3; reg.setRpValue( rp, reg.rpValue( rp ) + 1 ); break; } // inx bc/de
        case 0x33: sp++; break; // inx sp
        case 0x04: case 0x14: case 0x0c: case 0x1c: case 0x3c: { uint8_t * pdst = dst_address( op2 ); *pdst = op_inc( *pdst ); break; } // inr b/d/c/e/a
        case 0x05: case 0x15: case 0x0d: case 0x1d: case 0x3d: { uint8_t * pdst = dst_address( op2 ); *pdst = op_dec( *pdst ); break; } // dcr b/d/c/e/a
        case 0x06: case 0x16: case 0x0e: case 0x1e: case 0x3e: * dst_address( op2 ) = pcbyte( pc ); break; // mvi b/d/c/e/a, d8
        case 0x07: reg.fCarry = ( 0 != ( reg.a & 0x80 ) ); reg.a = (uint8_t) ( reg.a << 1 ); if ( reg.fCarry ) reg.a |= 1; reg.clearHN(); reg.z80_assignYX( reg.a ); break; // rlc
        case 0x0a: reg.z80_set_memptr( reg.B() + 1 ); reg.a = memory[ reg.B() ]; break; // ldax b
        case 0x0b: case 0x1b: { uint8_t rp = ( op2 >> 4 ) & 3; reg.setRpValue( rp, reg.rpValue( rp ) - 1 ); break; } // dcx bc/de
        case 0x3b: sp--; break; // dcx sp
        case 0x0f: reg.fCarry = ( 0 != ( reg.a & 1 ) ); reg.a = (uint8_t) ( reg.a >> 1 ); if ( reg.fCarry ) reg.a |= 0x80; reg.clearHN(); reg.z80_assignYX( reg.a ); break; // rrc
        case 0x12: memory[ reg.D() ] = reg.a; reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) | ( ( reg.D() + 1 ) & 0xff ) ); break; // stax d
        case 0x17: { bool c = reg.fCarry; reg.fCarry = ( 0 != ( 0x80 & reg.a ) ); reg.a = (uint8_t) ( reg.a << 1 ); if ( c ) reg.a |= 1; reg.clearHN(); reg.z80_assignYX( reg.a ); break; } // ral
        case 0x1a: reg.z80_set_memptr( reg.D() + 1 ); reg.a = memory[ reg.D() ]; break; // ldax d
        case 0x1f: { bool c = reg.fCarry; reg.fCarry = ( 0 != ( reg.a & 1 ) ); reg.a = (uint8_t) ( reg.a >> 1 ); if ( c ) reg.a |= 0x80; reg.clearHN(); reg.z80_assignYX( reg.a ); break; } // rar
        case 0x27: op_daa(); break;
        case 0x2f: op_cma(); break;
        case 0x32: { uint16_t addr = pcword( pc ); memory[ addr ] = reg.a; reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) | ( ( addr + 1 ) & 0xff ) ); break; } // sta a16
        case 0x37: reg.fCarry = 1; reg.clearHN(); reg.z80_assignYX( reg.a ); break; // stc
        case 0x3a: { uint16_t addr = pcword( pc ); reg.a = memory[ addr ]; reg.z80_set_memptr( addr + 1 ); break; } // lda a16
        case 0x3f: op_cmc(); break;
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x87: op_add( src_value( op2 ) ); break;
        case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8f: op_adc( src_value( op2 ) ); break; // adc
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x97: reg.a = op_sub( src_value( op2 ) ); break;
        case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9f: op_sbb( src_value( op2 ) ); break;
        case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa7: op_ana( src_value( op2 ) ); break;
        case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xaf: op_xra( src_value( op2 ) ); break;
        case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb7: op_ora( src_value( op2 ) ); break;
        case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbf: op_cmp( src_value( op2 ) ); break;
        case 0xc0: case 0xd0: case 0xe0: case 0xf0: case 0xc8: case 0xd8: case 0xe8: case 0xf8: // conditional return
            if ( check_conditional( op2 ) ) { pc = popword( sp ); reg.z80_set_memptr( pc ); }
            break;
        case 0xc1: case 0xd1: reg.setRpValueFromOp( op2, popword( sp ) ); break; // pop bc/de
        case 0xc2: case 0xd2: case 0xe2: case 0xf2: case 0xca: case 0xda: case 0xea: case 0xfa: // conditional jmp
        {
            uint16_t address = pcword( pc );
            reg.z80_set_memptr( address );
            if ( check_conditional( op2 ) )
                pc = address;
            break;
        }
        case 0xc3: { uint16_t address = pcword( pc ); reg.z80_set_memptr( address ); pc = address; break; } // jmp a16
        case 0xc4: case 0xd4: case 0xe4: case 0xf4: case 0xcc: case 0xdc: case 0xec: case 0xfc: // conditional call
        {
            uint16_t address = pcword( pc );
            reg.z80_set_memptr( address );
            if ( check_conditional( op2 ) )
            {
                pushword( sp, pc );
                pc = address;
            }
            break;
        }
        case 0xc5: case 0xd5: pushword( sp, reg.rpValueFromOp( op2 ) ); break; // push bc/de
        case 0xc6: op_add( pcbyte( pc ) ); break; // adi
        case 0xc7: case 0xd7: case 0xe7: case 0xf7: case 0xcf: case 0xdf: case 0xef: case 0xff: // rst
            pushword( sp, pc );
            pc = 0x38 & (uint16_t) op2;
            reg.z80_set_memptr( pc );
            break;
        case 0xc9: pc = popword( sp ); reg.z80_set_memptr( pc ); break; // ret
        case 0xcd: { uint16_t t = pcword( pc ); reg.z80_set_memptr( t ); pushword( sp, pc ); pc = t; break; } // call a16
        case 0xce: op_adc( pcbyte( pc ) ); break; // aci
        case 0xd3: { uint8_t port = pcbyte( pc ); x80_invoke_out( port ); reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) | ( ( port + 1 ) & 0xff ) ); break; } // out d8
        case 0xd6: reg.a = op_sub( pcbyte( pc ) ); break; // sui
        case 0xdb: { uint8_t port = pcbyte( pc ); reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) + port + 1 ); x80_invoke_in( port ); break; } // in d8
        case 0xde: op_sbb( pcbyte( pc ) ); break; // sbi
        case 0xe6: op_ana( pcbyte( pc ) ); break; // ani
        case 0xeb: { uint16_t t = reg.H(); reg.SetH( reg.D() ); reg.SetD( t ); break; } // xchg -- undocumented on real hardware: DD/FD-prefixed EX DE,HL is unaffected
        case 0xee: op_xra( pcbyte( pc ) ); break; // xri
        case 0xf1: reg.SetPSW( popword( sp ) ); break; // pop psw
        case 0xf3: reg.fINTE = false; reg.fINTE2 = false; break; // di
        case 0xf5: pushword( sp, reg.PSW() ); break; // push psw
        case 0xf6: op_ora( pcbyte( pc ) ); break; // ori
        case 0xfb: reg.fINTE = true; reg.fINTE2 = true; break; // ei
        case 0xfe: op_cmp( pcbyte( pc ) ); break; // cpi
        // Z80-only opcodes with no H/L involvement (EX AF,AF', DJNZ, JR
        // variants, EXX, ED-prefixed instructions) are dispatched exactly as
        // if unprefixed by reusing the existing, already-tested handler; ED
        // in particular is documented to always ignore a preceding DD/FD.
        case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: case 0xd9: case 0xed:
            cycles = z80_emulate( op2, pc, sp );
            break;
        default:
            z80_ni( op, op2 );
    }
    return cycles;
} //z80_execute_unprefixed

void z80_op_bit( uint8_t val, uint8_t bit, z80_value_source vs )
{
    assert( bit <= 7 );
    reg.fAuxCarry = true; // per doc
    reg.fWasSubtract = false;

    reg.fSign = ( ( 7 == bit ) && ( 0 != ( 0x80 & val ) ) ); // Zilog doc says fSign is "unknown", but hardware does this
    uint8_t cmp = ( 1 << bit );
    reg.fZero = ( 0 == ( val & cmp ) );
    reg.fParityEven_Overflow = reg.fZero;  // non-documented

    // Y and X are set from the source value, not as 4.1 from "The Undocumented Z80 Documented"
    // has from the value resulting from the bit operation. But only if the source is a register.

    if ( vs_register == vs )
        reg.z80_assignYX( val );
    else
        reg.z80_assignYX_from_memptr();
} //z80_op_bit

void z80_op_rlc( uint8_t * pval )
{
    // rotate left carry. 7 bit to both C and 0 bit. flags: S, Z, H reset, Parity, N reset, and C

    uint8_t x = *pval;
    bool bit7 = ( 0 != ( x & 0x80 ) );
    x <<= 1;
    reg.fCarry = bit7;
    if ( bit7 )
        x |= 1;
    set_sign_zero_parity( x );
    reg.fAuxCarry = false;
    reg.fWasSubtract = false;
    reg.z80_assignYX( x );
    *pval = x;
} //z80_op_rlc

void z80_op_rl( uint8_t * pval )
{
    // rotate left. 7 bit to C. Old carry bit to 0. flags: S, Z, H reset, Parity, N reset, and C

    uint8_t x = *pval;
    bool bit7 = ( 0 != ( x & 0x80 ) );
    x <<= 1;
    if ( reg.fCarry )
        x |= 1;
    reg.fCarry = bit7;
    set_sign_zero_parity( x );
    reg.fAuxCarry = false;
    reg.fWasSubtract = false;
    reg.z80_assignYX( x );
    *pval = x;
} //z80_op_rl

void z80_op_rrc( uint8_t * pval )
{
    // rotate right carry. 0 bit to both C and 7 bit. flags: S, Z, H reset, Parity, N reset, and C

    uint8_t x = *pval;
    bool bit0 = ( 0 != ( x & 1 ) );
    x >>= 1;
    reg.fCarry = bit0;
    if ( bit0 )
        x |= 0x80;
    set_sign_zero_parity( x );
    reg.fAuxCarry = false;
    reg.fWasSubtract = false;
    reg.z80_assignYX( x );
    *pval = x;
} //z80_op_rrc

void z80_op_rr( uint8_t * pval )
{
    // rotate right. 0 bit to C. Old C to 7 bit. flags: S, Z, H reset, Parity, N reset, and C

    uint8_t x = *pval;
    bool bit0 = ( 0 != ( x & 1 ) );
    x >>= 1;
    if ( reg.fCarry )
        x |= 0x80;
    reg.fCarry = bit0;
    set_sign_zero_parity( x );
    reg.fAuxCarry = false;
    reg.fWasSubtract = false;
    reg.z80_assignYX( x );
    *pval = x;
} //z80_op_rr

uint16_t z80_op_sub_16( uint16_t lhs, uint16_t rhs, bool borrow = false )
{
    // com == ones-complement

    uint16_t com_rhs = ~rhs;
    uint16_t borrow_int = borrow ? 0 : 1;
    uint32_t res32 =  (uint32_t) lhs + (uint32_t) com_rhs + (uint32_t) borrow_int;
    uint16_t res16 = res32 & 0xffff;

    reg.fCarry = ( 0 == ( res32 & 0x10000 ) );
    z80_set_sign_zero_16( res16 );

    // if not ( ( one of lhs and com_rhs are negative ) and ( one of lhs and result are negative ) )

    reg.fParityEven_Overflow = ! ( ( lhs ^ com_rhs ) & 0x8000 ) && ( ( lhs ^ res16 ) & 0x8000 );
    reg.fWasSubtract = true;
    if ( borrow )
        rhs++;
    reg.fAuxCarry = ( ( rhs & 0xfff ) > ( lhs & 0xfff ) );
    reg.z80_assignYX( res16 >> 8 );

    return res16;
} //z80_op_sub_16

uint16_t z80_op_add_16( uint16_t a, uint16_t b )
{
    uint32_t resultAux = ( (uint32_t) ( a & 0xfff ) + (uint32_t) ( b & 0xfff ) );
    reg.fAuxCarry = ( 0 != ( resultAux & 0xfffff000 ) );
    reg.fWasSubtract = false;

    uint32_t result = ( (uint32_t) a + (uint32_t) b );
    reg.fCarry = ( 0 != ( result & 0x10000 ) );
    reg.z80_assignYX( (uint8_t) ( result >> 8 ) );

    return (uint16_t) result;
} //z80_op_add_16

uint16_t z80_op_adc_16( uint16_t l, uint16_t r )
{
    uint16_t carryOut, result, resultAux;

    if ( reg.fCarry )
    {
        carryOut = ( l >= ( 0xffff - r ) );
        result = r + l + 1;
        resultAux = ( 0xfff & r ) + ( 0xfff & l ) + 1;
    }
    else
    {
        carryOut = ( l > ( 0xffff - r ) );
        result = r + l;
        resultAux = ( 0xfff & r ) + ( 0xfff & l );
    }

    reg.z80_assignYX( result >> 8 );
    reg.fAuxCarry = ( 0 != ( 0xf000 & resultAux ) );
    uint16_t carryIns = result ^ l ^ r;
    z80_set_sign_zero_16( result );
    reg.fParityEven_Overflow = ( carryIns >> 15 ) ^ carryOut;
    reg.fCarry = ( 0 != carryOut );
    reg.fWasSubtract = false;
    return result;
} //z80_op_adc_16

void z80_op_sla( uint8_t * pval )
{
    uint8_t val = *pval;
    reg.fCarry = ( 0 != ( val & 0x80 ) );
    val <<= 1;
    set_sign_zero_parity( val );
    reg.clearHN();
    reg.z80_assignYX( val );
    *pval = val;
} //z80_op_sla

void z80_op_sll( uint8_t * pval ) // not a documented opcode
{
    uint8_t val = *pval;
    reg.fCarry = ( 0 != ( val & 0x80 ) );
    val <<= 1;
    val |= 1;
    set_sign_zero_parity( val );
    reg.clearHN();
    reg.z80_assignYX( val );
    *pval = val;
} //z80_op_sll

void z80_op_sra( uint8_t * pval )
{
    uint8_t val = *pval;
    reg.fCarry = ( 0 != ( val & 1 ) );
    val >>= 1;
    val |= ( ( *pval ) & 0x80 ); // leave high bit unchanged
    set_sign_zero_parity( val );
    reg.clearHN();
    reg.z80_assignYX( val );
    *pval = val;
} //z80_op_sra

void z80_op_srl( uint8_t * pval )
{
    uint8_t val = *pval;
    reg.fCarry = ( val & 1 );
    val >>= 1;
    set_sign_zero_parity( val );
    reg.clearHN();
    reg.z80_assignYX( val );
    *pval = val;
} //z80_op_srl

X80_HOT_CODE uint16_t z80_emulate( uint8_t op, uint16_t & pc, uint16_t & sp )    // this is just for instructions that aren't shared with 8080
{
    uint16_t opaddress = pc - 1;
    uint8_t op2 = memory[ pc ];
    uint8_t op3 = memory[ pc + 1 ];
    uint8_t op4 = memory[ pc + 2 ];
    int op3int = (int) (int8_t) op3;
    uint16_t cycles = 4; // general-purpose default

    switch ( op )
    {
        case 0x08: // ex af and af'
        {
            swap( reg.a, reg.ap );
            reg.materializeFlags();
            swap( reg.f, reg.fp );
            reg.unmaterializeFlags();
            break;
        }
        case 0x10: // djnz
        {
            uint8_t offset = pcbyte( pc );
            reg.b = reg.b - 1;
            if ( 0 != reg.b )
            {
                pc = opaddress + 2 + (int16_t) (int8_t) offset;
                reg.z80_set_memptr( pc );
                cycles = 3;
            }
            else
                cycles = 2;
            break;
        }
        case 0x18: // jr n
        {
            uint8_t offset = pcbyte( pc );
            pc = opaddress + 2 + (int16_t) (int8_t) offset;
            reg.z80_set_memptr( pc );
            cycles = 3;
            break;
        }
        case 0x20: // jr nz, n
        {
            uint8_t offset = pcbyte( pc );
            if ( !reg.fZero )
            {
                pc = opaddress + 2 + (int16_t) (int8_t) offset;
                reg.z80_set_memptr( pc );
                cycles = 3;
            }
            else
                cycles = 2;
            break;
        }
        case 0x28: // jr z, n
        {
            uint8_t offset = pcbyte( pc );
            if ( reg.fZero )
            {
                pc = opaddress + 2 + (int16_t) (int8_t) offset;
                reg.z80_set_memptr( pc );
                cycles = 3;
            }
            else
                cycles = 2;
            break;
        }
        case 0x30: // jr nc, n
        {
            uint8_t offset = pcbyte( pc );
            if ( !reg.fCarry )
            {
                pc = opaddress + 2 + (int16_t) (int8_t) offset;
                reg.z80_set_memptr( pc );
                cycles = 3;
            }
            else
                cycles = 2;
            break;
        }
        case 0x38: // jr c, n
        {
            uint8_t offset = pcbyte( pc );
            if ( reg.fCarry )
            {
                pc = opaddress + 2 + (int16_t) (int8_t) offset;
                reg.z80_set_memptr( pc );
                cycles = 3;
            }
            else
                cycles = 2;
            break;
        }
        case 0xcb: // rotate / bits
        {
            pcbyte( pc ); // get past op2

            if ( 0x20 == ( op2 & 0xf8 ) ) // sla
            {
                cycles = 3;
                uint8_t rm = op2 & 0x7;
                if ( 6 == rm )
                    cycles += 2;
                uint8_t * pdst = dst_address_rm( rm );
                z80_op_sla( pdst );
            }
            else if ( 0x28 == ( op2 & 0xf8 ) ) // sra
            {
                cycles = 3;
                uint8_t rm = op2 & 0x7;
                if ( 6 == rm )
                    cycles += 2;
                uint8_t * pdst = dst_address_rm( rm );
                z80_op_sra( pdst );
            }
            else if ( op2 >= 0x30 && op2 <= 0x3f )
            {
                uint8_t rm = op2 & 0x7;
                uint8_t * pdst = dst_address_rm( rm );
                if ( op2 <= 0x37 )
                    z80_op_sll( pdst );
                else
                    z80_op_srl( pdst );
            }
            else if ( 0x38 == ( op2 & 0xf8 ) ) // srl r = shift right logical
            {
                cycles = 3;
                uint8_t rm = op2 >> 4;
                if ( 6 == rm )
                    cycles += 2;
                uint8_t * pdst = dst_address_rm( rm );
                z80_op_srl( pdst );
            }
            else if ( op2 >= 0x40 && op2 <= 0x7f ) // bit #, rm
            {
                cycles = 3;
                uint8_t rm = op2 & 0x7;
                if ( 6 == rm )
                    cycles += 4;
                uint8_t bit = ( op2 >> 3 ) & 0x7;
                uint8_t val = src_value_rm( rm );
                if ( 6 == rm )
                    reg.z80_set_memptr( reg.H() + 1 );
                z80_op_bit( val, bit, ( 6 == rm ) ? vs_memory : vs_register );
            }
            else if ( op2 >= 0x80 && op2 <= 0xbf ) // res bit #, rm  AKA reset
            {
                cycles = 3;
                uint8_t rm = op2 & 0x7;
                if ( 6 == rm )
                    cycles += 7;
                uint8_t bit = ( op2 >> 3 ) & 0x7;
                uint8_t val = src_value_rm( rm );
                uint8_t mask = ~ ( 1 << bit );
                val &= mask;
                * dst_address_rm( rm ) = val;
            }
            else if ( op2 >= 0xc0 && op2 <= 0xff ) // set bit #, rm
            {
                cycles = 8;
                uint8_t rm = op2 & 0x7;
                if ( 6 == rm )
                    cycles += 7;
                uint8_t bit = ( op2 >> 3 ) & 0x7;
                uint8_t val = src_value_rm( rm );
                uint8_t mask = 1 << bit;
                val |= mask;
                * dst_address_rm( rm ) = val;
            }
            else if ( op2 <= 0x1f ) // rlc, rrc, rl, rr on rm
            {
                cycles = 8;
                uint8_t mod = op2;
                uint8_t rot = ( mod >> 3 ) & 0x3;
                uint8_t rm = mod & 0x7;
                if ( 6 == rm )
                    cycles += 2;
                uint8_t * pval = dst_address_rm( rm );
                if ( 0 == rot )
                    z80_op_rlc( pval );
                else if ( 1 == rot )
                    z80_op_rrc( pval );
                else if ( 2 == rot )
                    z80_op_rl( pval );
                else // if ( 3 == rot )
                    z80_op_rr( pval );
            }
            else
                z80_ni( op, op2 );
            break;
        }
        case 0xd9: // exx   B, D, H with B', D', H'
        {
            swap( reg.b, reg.bp );
            swap( reg.c, reg.cp );
            swap( reg.d, reg.dp );
            swap( reg.e, reg.ep );
            swap( reg.h, reg.hp );
            swap( reg.l, reg.lp );
            break;
        }
        case 0xdd: case 0xfd: // ix & iy operations
        {
            reg.z80_bump_r();
            pcbyte( pc ); // consume op2: the dd or fd

            // Prefixed HALT (DD 76 / FD 76) executes as plain HALT with the
            // DD/FD prefix ignored: opcode 0x76 occupies the otherwise
            // unused "LD (HL),(HL)" slot in the LD r,(HL)/LD (HL),r opcode
            // grid, repurposed by the hardware as HALT instead, so it was
            // never an (HL) reference to begin with and DD/FD has no effect
            // on it (see e.g. Sean Young's "The Undocumented Z80
            // Documented"). This emulator cannot easily replicate that: the
            // real, unprefixed 0x76 case
            // (below, in x80_emulate_budget) stops its instruction-fetch
            // loop immediately via `goto _all_done`, a label with scope
            // local to that function's loop; z80_emulate (this function) is
            // invoked as an ordinary subroutine call from that loop's
            // default case and has no way to signal "stop the batch now"
            // back to its caller. Rather than approximate real hardware
            // with an inaccurate implementation (e.g. setting the halted
            // flag but letting the current batch's loop keep running past
            // it), prefixed HALT is deliberately left as not-implemented
            // here, matching the pre-existing, disclosed limitation.
            //
            // This check must run first, before any of the mask-based
            // classifiers below: 0x76 (binary 0111 0110) incidentally
            // satisfies the "ld r,(i+d)" mask 0x46==(op2&0xc7) the same way
            // the true match 0x46 does, purely by bit-pattern coincidence,
            // and would otherwise be silently misinterpreted as a load from
            // (IX+d) into (HL) instead of ever reaching this check.
            if ( 0x76 == op2 )
            {
                z80_ni( op, op2 );
                break;
            }

            if ( 0x21 == op2 )  // ld ix/iy word
            {
                if ( 0xdd == op )
                    reg.ix = pcword( pc );
                else
                    reg.iy = pcword( pc );
            }
            else if ( 0x22 == op2 ) // ld (address), ix/iy
            {
                uint16_t address = pcword( pc );
                setmword( address, reg.z80_getIndex( op ) );
                reg.z80_set_memptr( address + 1 );
            }
            else if ( 0x23 == op2 ) // inc ix/iy     no flags are affected
            {
                cycles = 10;
                if ( 0xdd == op )
                    reg.ix++;
                else
                    reg.iy++;
            }
            else if ( 0x26 == op2 ) // ld ix/iy h. not documented
                * reg.z80_getIndexByteAddress( op, 0 ) = pcbyte( pc );
            else if ( 0x2a == op2 )  // ld ix, (address)
            {
                uint16_t address = pcword( pc );
                reg.z80_setIndex( op, mword( address ) );
                reg.z80_set_memptr( address + 1 );
            }
            else if ( 0x2b == op2 ) // dec ix/iy   no flags are affected
            {
                if ( 0xdd == op )
                    reg.ix--;
                else
                    reg.iy--;
            }
            else if ( 0x2e == op2 ) // ld ix/iy l. not documented
                * reg.z80_getIndexByteAddress( op, 1 ) = pcbyte( pc );
            else if ( 0x34 == op2 ) // inc (i + index)
            {
                cycles = 6;
                uint16_t i = reg.z80_getIndex( op ) + (int16_t) (int8_t) pcbyte( pc );
                reg.z80_set_memptr( i );
                uint8_t x = memory[ i ];
                memory[i] = op_inc( x );
            }
            else if ( 0x35 == op2 ) // dec (i + index)
            {
                cycles = 6;
                uint16_t i = reg.z80_getIndex( op ) + (int16_t) (int8_t) pcbyte( pc );
                reg.z80_set_memptr( i );
                uint8_t x = memory[ i ];
                memory[ i ] = op_dec( x );
            }
            else if ( 0x36 == op2 )  // ld (ix/iy + index), immediate byte
            {
                cycles = 5;
                uint16_t i = reg.z80_getIndex( op ) + (int16_t) (int8_t) pcbyte( pc );
                reg.z80_set_memptr( i );
                uint8_t val = pcbyte( pc );
                memory[ i ] = val;
            }
            else if ( ( ( op2 >= 0x40 && op2 <= 0x6f ) || ( op2 >= 0x78 && op2 <= 0x7f ) ) && // ld [bcdeIhIla][bcdeIhIla]
                      ( ( ( op2 & 0xf ) != 6 ) && ( ( op2 & 0xf ) != 0xe ) ) )
            {
                uint8_t fromval = op2 & 0xf;
                if ( fromval >= 8 )
                    fromval -= 8;

                uint8_t tmp = ( 0 == fromval ) ? reg.b :
                              ( 1 == fromval ) ? reg.c :
                              ( 2 == fromval ) ? reg.d :
                              ( 3 == fromval ) ? reg.e :
                              ( 4 == fromval ) ? reg.z80_getIndexByte( op, 0 ) :
                              ( 5 == fromval ) ? reg.z80_getIndexByte( op, 1 ) :
                              reg.a;

                if ( op2 <= 0x47 )
                    reg.b = tmp;
                else if ( op2 <= 0x4f )
                    reg.c = tmp;
                else if ( op2 <= 0x57 )
                    reg.d = tmp;
                else if ( op2 <= 0x5f )
                    reg.e = tmp;
                else if ( op2 <= 0x67 )
                    *reg.z80_getIndexByteAddress( op, 0 ) = tmp;
                else if ( op2 <= 0x6f )
                    *reg.z80_getIndexByteAddress( op, 1 ) = tmp;
                else
                    reg.a = tmp;
            }
            else if ( 0x46 == ( op2 & 0xc7 ) ) // ld r, (i + #). must include bit 7 in the mask or this
                                                // wrongly also matches the ALU-immediate opcodes
                                                // 0xc6/ce/d6/de/e6/ee/f6/fe (adi/aci/sui/sbi/ani/xri/ori/cpi)
            {
                cycles = 5;
                pcbyte( pc ); // consume op3
                uint16_t address = reg.z80_getIndex( op ) + (uint16_t) op3int;
                reg.z80_set_memptr( address );
                * dst_address( op2 ) = memory[ address ];
            }
            else if ( 0x70 == ( op2 & 0xf8 ) )  // ld (i+#), r/#
            {
                cycles = 5;
                pcbyte( pc ); // consume op3

                // if 6, there is an op4 for the index (not hl-indexed memory); otherwise use a register value

                uint8_t src = op2 & 0x7;
                uint8_t val = ( 6 == src ) ? pcbyte( pc ) : src_value_rm( src );
                uint16_t i = reg.z80_getIndex( op );
                i += (uint16_t) op3int;
                reg.z80_set_memptr( i );
                memory[ i ] = val;
            }
            else if ( 0x84 == ( op2 & 0xc6 ) ) // math on ixh/ixl/iyh/iyl with a. 84/85/8c/8d/94/95/9c/9d/a4/a5/ac/ad/b4/b5/bc/bd.
                                                // the mask must include bit 2 (0xc2 wrongly also matched reg B/C forms
                                                // like 0x80/0x81/0x88/0x89/etc, which don't reference h/l at all)
            {
                uint8_t value = reg.z80_getIndexByte( op, op2 & 1 );
                op_math( op2, value );
            }
            else if ( 0x86 == ( op2 & 0xc7 ) ) // math on [ ix/iy + index ]
            {
                cycles = 5;
                uint16_t x = reg.z80_getIndex( op );
                x += (int16_t) (int8_t) pcbyte( pc );
                reg.z80_set_memptr( x );
                op_math( op2, memory[ x ] );
            }
            else if ( 0x24 == ( op2 & 0xf6 ) ) // inc/dec ixh, ixl, iyh, iyl
            {
                uint8_t *pval = reg.z80_getIndexByteAddress( op, ( op2 >> 3 ) & 1 );
                if ( op2 & 1 )
                    *pval = op_dec( *pval );
                else
                    *pval = op_inc( *pval );
            }
            else if ( 0x09 == ( op2 & 0xcf ) ) // add ix/iy, rp
            {
                // only sets H (carry from bit 11) and C (carry from bit 15) flags. ignores C flag on input. N is reset
                // for add ix, rp is 0..3 bc, de, ix, sp.
                // for add iy, rp is 0..3 bc, de, iy, sp.

                uint8_t rp = ( 0x3 & ( op2 >> 4 ) );
                uint16_t rpval = ( 3 == rp ) ? sp : reg.rpValue( rp );
                if ( 2 == rp )
                    rpval = ( 0xdd == op ) ? reg.ix : reg.iy;

                uint16_t oldval = reg.z80_getIndex( op );
                uint16_t newval = z80_op_add_16( oldval, rpval );
                reg.z80_setIndex( op, newval );
                reg.z80_set_memptr( oldval + 1 );
            }
            else if ( 0xcb == op2 ) // bit operations
            {
                reg.z80_bump_r();
                if ( 0x26 == op4 || 0x2e == op4 || 0x3e == op4 ) // sla, sra, srl [ix/iy + offset]
                {
                    uint8_t offset = pcbyte( pc ); // the op3
                    pcbyte( pc ); // the op4
                    uint16_t index = reg.z80_getIndex( op );
                    index += (int16_t) (int8_t) offset;
                    reg.z80_set_memptr( index );
                    if ( 0x26 == op4 ) // sla
                        z80_op_sla( & memory[ index ] );
                    else if ( 0x2e == op4 ) // sra
                        z80_op_sra( & memory[ index ] );
                    else // ( 0x3e == op4 ) srl
                        z80_op_srl( & memory[ index ] );
                }
                else if ( op4 <= 0x3f ) // bit shift on memory
                {
                    cycles = 8; // this is a guess -- it's undocumented
                    uint8_t offset = pcbyte( pc );
                    pcbyte( pc );
                    uint16_t index = reg.z80_getIndex( op );
                    index += (int16_t) (int8_t) offset;
                    reg.z80_set_memptr( index );

                    if ( op4 <= 0x07 )
                        z80_op_rlc( & memory[ index ] );
                    else if ( op4 <= 0x0f )
                        z80_op_rrc( & memory[ index ] );
                    else if ( op4 <= 0x17 )
                        z80_op_rl( & memory[ index ] );
                    else if ( op4 <= 0x1f )
                        z80_op_rr( & memory[ index ] );
                    else if ( op4 <= 0x27 )
                        z80_op_sla( & memory[ index ] );
                    else if ( op4 <= 0x2f )
                        z80_op_sra( & memory[ index ] );
                    else if ( op4 <= 0x37 )
                        z80_op_sll( & memory[ index ] );
                    else if ( op4 <= 0x3f )
                        z80_op_srl( & memory[ index ] );

                    uint8_t rm = op4 & 0x7;
                    if ( 6 != rm )          // no write to memory variant
                        * dst_address_rm( rm ) = memory[ index ];
                }
                else if ( ( ( op4 & 0xf ) == 0xe ) || ( ( op4 & 0xf ) == 0x6 ) ) // bit/res/set b, (ix/iy + d)
                {
                    cycles = 8;
                    uint8_t index = pcbyte( pc );
                    uint8_t mod = pcbyte( pc );
                    uint8_t bit = ( mod >> 3 ) & 0x7;
                    uint8_t mask = 1 << bit;
                    uint8_t top2bits = mod & 0xc0;
                    uint16_t offset = reg.z80_getIndex( op ) + (int16_t) (int8_t) index;
                    reg.z80_set_memptr( offset );
                    uint8_t val = memory[ offset ];

                    if ( 0x40 == top2bits ) // bit
                    {
                        cycles++;
                        z80_op_bit( val, bit, vs_indexed );
                    }
                    else if ( 0x80 == top2bits ) // reset
                    {
                        mask = ~mask;
                        val &= mask;
                        memory[ offset ] = val;
                    }
                    else if ( 0xc0 == top2bits ) // set
                    {
                        val |= mask;
                        memory[ offset ] = val;
                    }
                    else if ( 0x00 == top2bits ) // rlc/rl/rrc/rr rotate of (i + index)
                    {
                        if ( 0x06 == mod )
                            z80_op_rlc( & memory[ offset ] );
                        else if ( 0x16 == mod )
                            z80_op_rl( & memory[ offset ] );
                        else if ( 0x0e == mod )
                            z80_op_rrc( & memory[ offset ] );
                        else if ( 0x1e == mod )
                            z80_op_rr( & memory[ offset ] );
                        else
                            z80_ni( op, op2 );
                    }
                    else
                        z80_ni( op, op2 );
                }
            }
            else if ( 0xe1 == op2 ) // pop ix/iy
            {
                cycles = 14;
                uint16_t val = popword( sp );
                reg.z80_setIndex( op, val );
            }
            else if ( 0xe3 == op2 ) // ex (sp), ix/iy
            {
                cycles = 23;
                uint16_t val = reg.z80_getIndex( op );
                reg.z80_setIndex( op, mword( sp ) );
                setmword( sp, val );
                reg.z80_set_memptr( reg.z80_getIndex( op ) );
            }
            else if ( 0xe5 == op2 )  // push ix/iy
            {
                cycles = 15;
                uint16_t val = reg.z80_getIndex( op );
                pushword( sp, val );
            }
            else if ( 0xe9 == op2 ) // jp (ix/iy) // the Z80 name makes it look indirect. It's not.
            {
                cycles = 8;
                pc = reg.z80_getIndex( op );
            }
            else if ( 0xf9 == op2 ) // ld sp, ix/iy
            {
                cycles = 10;
                sp = reg.z80_getIndex( op );
            }
            else
                // 0x76 and 0x64 are already excluded above, before any of the
                // masks in this chain could misclassify them.
                cycles = z80_execute_unprefixed( op, op2, pc, sp );
            break;
        }
        case 0xed: // 16-bit load/store and i/o operations
        {
            reg.z80_bump_r();
            pcbyte( pc );  // consume op2

            if ( 0x46 == op2 || 0x4e == op2 || 0x66 == op2 || 0x6e == op2 ) // im 0
                reg.interruptMode = 0;
            else if ( 0x56 == op2 || 0x76 == op2 ) // im 1
                reg.interruptMode = 1;
            else if ( 0x5e == op2 || 0x7e == op2 ) // im 2
                reg.interruptMode = 2;
            else if ( 0x45 == ( op2 & 0xcf ) || 0x4d == ( op2 & 0xcf ) ) // retn / reti and aliases
            {
                pc = popword( sp );
                reg.z80_set_memptr( pc );
                reg.fINTE = reg.fINTE2;
            }
            else if ( 0x43 == ( op2 & 0xcf ) ) // ld (mw), rp AKA ld (nn), dd
            {
                cycles = 8;
                uint8_t rp = ( op2 >> 4 ) & 3;
                uint16_t addr = pcword( pc );
                setmword( addr, ( 3 == rp ) ? sp : reg.rpValue( rp ) );
                reg.z80_set_memptr( addr + 1 );
            }
            else if ( 0x4b == ( op2 & 0xcf ) ) // ld rp, (nn) AKA ld dd, (nn)
            {
                cycles = 8;
                uint8_t rp = ( op2 >> 4 ) & 3;
                uint16_t addr = pcword( pc );
                uint16_t value = mword( addr );
                reg.z80_set_memptr( addr + 1 );
                if ( 3 == rp )
                    sp = value;
                else
                    reg.setRpValue( rp, value );
            }
            else if ( 0x44 == ( op2 & 0xc7 ) ) // neg and undocumented aliases
            {
                cycles = 8;
                uint8_t prior = reg.a;
                reg.a = 0 - reg.a;
                set_sign_zero( reg.a );
                reg.fParityEven_Overflow = ( 0x80 == prior );
                reg.fWasSubtract = true;
                reg.fCarry = ( 0 != prior );
                reg.fAuxCarry = ( 0 != ( prior & 0xf ) );
                reg.z80_assignYX( reg.a );
            }
            else if ( 0x47 == op2 ) // ld i,a
                reg.i = reg.a;
            else if ( 0x4f == op2 ) // ld r,a
                reg.r = reg.a;
            else if ( 0x57 == op2 ) // ld a,i
            {
                reg.a = reg.i;
                set_sign_zero( reg.a );
                reg.fParityEven_Overflow = reg.fINTE2;
                reg.fWasSubtract = false;
                reg.fAuxCarry = false;
                reg.z80_assignYX( reg.a );
            }
            else if ( 0x5f == op2 ) // ld a,r
            {
                reg.a = ( 0x7f & reg.r ); // the high bit is always 0 on Z80
                set_sign_zero( reg.a );
                reg.fParityEven_Overflow = false; // no iff2
                reg.fWasSubtract = false;
                reg.fAuxCarry = false;
                reg.z80_assignYX( reg.a );
            }
            else if ( 0x67 == op2 ) // rrd
            {
                uint8_t mem = memory[ reg.H() ];
                uint8_t a = reg.a;

                reg.a = ( a & 0xf0 ) | ( mem & 0x0f );
                uint8_t newmem = ( ( a << 4 ) & 0xf0 ) | ( ( mem >> 4 ) & 0x0f );
                memory[ reg.H() ] = newmem;

                set_sign_zero_parity( reg.a );
                reg.clearHN();
                reg.z80_assignYX( reg.a );
                reg.z80_set_memptr( reg.H() + 1 );
            }
            else if ( 0x6f == op2 ) // rld
            {
                uint8_t mem = memory[ reg.H() ];
                uint8_t a = reg.a;

                uint8_t newmem = ( ( mem << 4 ) & 0xf0 ) | ( a & 0x0f );
                reg.a = ( ( mem >> 4 ) & 0xf ) | ( a & 0xf0 );
                memory[ reg.H() ] = newmem;

                set_sign_zero_parity( reg.a );
                reg.clearHN();
                reg.z80_assignYX( reg.a );
                reg.z80_set_memptr( reg.H() + 1 );
            }
            else if ( 0x4a == ( op2 & 0xcf ) ) // adc hl, rp
            {
                uint8_t rp = ( op2 >> 4 ) & 3;
                uint16_t oldH = reg.H();
                uint16_t result = z80_op_adc_16( oldH, ( 3 == rp ) ? sp : reg.rpValue( rp ) );
                reg.SetH( result );
                reg.z80_set_memptr( oldH + 1 );
            }
            else if ( 0xa0 == op2 ) // ldi
            {
                cycles = 4;
                uint16_t hl = reg.H();
                uint16_t de = reg.D();
                uint16_t bc = reg.B() - 1;
                uint8_t value = memory[ hl ];
                memory[ de ] = value;
                reg.fY = ( 0 != ( ( value + reg.a ) & 0x02 ) );
                reg.fX = ( 0 != ( ( value + reg.a ) & 0x08 ) );
                reg.SetH( hl + 1 );
                reg.SetD( de + 1 );
                reg.SetB( bc );
                reg.fParityEven_Overflow = ( 0 != bc );
                reg.fAuxCarry = 0;
                reg.fWasSubtract = 0;
            }
            else if ( 0xa1 == op2 ) // cpi
            {
                bool oldCarry = reg.fCarry;
                cycles = 4;
                uint16_t hl = reg.H();
                uint16_t bc = reg.B() - 1;
                uint8_t memval = memory[ hl ];
                op_cmp( memval );
                reg.SetH( hl + 1 );
                reg.SetB( bc );
                reg.fParityEven_Overflow = ( 0 != bc );
                uint8_t n = reg.a - memval - ( reg.fAuxCarry ? 1 : 0 ); // n = A - (HL) - HF
                reg.fY = ( 0 != ( n & 0x02 ) );
                reg.fX = ( 0 != ( n & 0x08 ) );
                reg.fCarry = oldCarry; // carry is not affected
                reg.z80_set_memptr( reg.memptr + 1 );
            }
            else if ( 0xa8 == op2 ) // ldd
            {
                cycles = 4;
                uint16_t hl = reg.H();
                uint16_t de = reg.D();
                uint16_t bc = reg.B() - 1;
                uint8_t value = memory[ hl ];
                memory[ de ] = value;
                reg.fY = ( 0 != ( ( value + reg.a ) & 0x02 ) );
                reg.fX = ( 0 != ( ( value + reg.a ) & 0x08 ) );
                reg.SetH( hl - 1 );
                reg.SetD( de - 1 );
                reg.SetB( bc );
                reg.fParityEven_Overflow = ( 0 != bc );
                reg.clearHN();
            }
            else if ( 0xa9 == op2 ) // cpd
            {
                bool oldCarry = reg.fCarry;
                cycles = 4;
                uint16_t hl = reg.H();
                uint16_t bc = reg.B() - 1;
                uint8_t memval = memory[ hl ];
                op_cmp( memval );
                reg.SetH( hl - 1 );
                reg.SetB( bc );
                reg.fParityEven_Overflow = ( 0 != bc );
                uint8_t n = reg.a - memval - ( reg.fAuxCarry ? 1 : 0 ); // n = A - (HL) - HF
                reg.fY = ( 0 != ( n & 0x02 ) );
                reg.fX = ( 0 != ( n & 0x08 ) );
                reg.fCarry = oldCarry; // carry is not affected
                reg.z80_set_memptr( reg.memptr - 1 );
            }
            else if ( 0xb0 == op2 ) // ldir
            {
                uint16_t hl = reg.H();
                uint16_t de = reg.D();
                uint16_t bc = reg.B() - 1;
                uint8_t value = memory[ hl++ ];
                memory[ de++ ] = value;
                reg.fY = ( 0 != ( ( value + reg.a ) & 0x02 ) );
                reg.fX = ( 0 != ( ( value + reg.a ) & 0x08 ) );
                reg.SetH( hl );
                reg.SetD( de );
                reg.SetB( bc );
                reg.fParityEven_Overflow = ( 0 != bc );
                reg.fAuxCarry = 0;
                reg.fWasSubtract = 0;
                if ( 0 != bc )
                {
                    reg.z80_set_memptr( pc - 1 );
                    cycles = 5;
                    pc -= 2;
                }
                else
                    cycles = 4;
            }
            else if ( 0xb1 == op2 ) // cpir
            {
                bool oldCarry = reg.fCarry;
                uint16_t hl = reg.H();
                uint16_t bc = reg.B() - 1;
                uint8_t memval = memory[ hl++ ];
                op_cmp( memval );
                uint8_t n = reg.a - memval - ( reg.fAuxCarry ? 1 : 0 ); // n = A - (HL) - HF
                reg.fY = ( 0 != ( n & 0x02 ) );
                reg.fX = ( 0 != ( n & 0x08 ) );
                reg.SetH( hl );
                reg.SetB( bc );
                reg.fParityEven_Overflow = ( 0 != bc ); // not what the Zilog doc says, but it's what works
                reg.fCarry = oldCarry; // carry is not affected
                if ( !reg.fZero && ( 0 != bc ) )
                {
                    reg.z80_set_memptr( pc - 1 );
                    cycles = 5;
                    pc -= 2;
                }
                else
                {
                    reg.z80_set_memptr( reg.memptr + 1 );
                    cycles = 4;
                }
            }
            else if ( 0xb8 == op2 ) // lddr
            {
                uint16_t hl = reg.H();
                uint16_t de = reg.D();
                uint16_t bc = reg.B() - 1;
                uint8_t value = memory[ hl-- ];
                memory[ de-- ] = value;
                reg.fY = ( 0 != ( ( value + reg.a ) & 0x02 ) );
                reg.fX = ( 0 != ( ( value + reg.a ) & 0x08 ) );
                reg.SetH( hl );
                reg.SetD( de );
                reg.SetB( bc );
                reg.fParityEven_Overflow = ( 0 != bc );
                reg.clearHN();
                if ( 0 != bc )
                {
                    reg.z80_set_memptr( pc - 1 );
                    cycles = 5;
                    pc -= 2;
                }
                else
                    cycles = 4;
            }
            else if ( 0xb9 == op2 ) // cpdr
            {
                bool oldCarry = reg.fCarry;
                uint16_t hl = reg.H();
                uint16_t bc = reg.B() - 1;
                uint8_t memval = memory[ hl-- ];
                op_cmp( memval );
                uint8_t n = reg.a - memval - ( reg.fAuxCarry ? 1 : 0 ); // n = A - (HL) - HF
                reg.fY = ( 0 != ( n & 0x02 ) );
                reg.fX = ( 0 != ( n & 0x08 ) );
                reg.SetH( hl );
                reg.SetB( bc );
                reg.fParityEven_Overflow = ( 0 != bc );
                reg.fCarry = oldCarry; // carry is not affected
                if ( !reg.fZero && ( 0 != bc ) )
                {
                    reg.z80_set_memptr( pc - 1 );
                    cycles = 5;
                    pc -= 2;
                }
                else
                {
                    reg.z80_set_memptr( reg.memptr - 1 );
                    cycles = 4;
                }
            }
            else if ( 0xa2 == op2 || 0xaa == op2 || // ini / ind
                      0xa3 == op2 || 0xab == op2 )   // outi / outd
            {
                bool input = ( 0 == ( op2 & 1 ) );
                bool decrement = ( 0 != ( op2 & 8 ) );
                uint16_t hl = reg.H();
                uint8_t old_a = reg.a;
                if ( input )
                {
                    reg.z80_set_memptr( reg.B() + ( decrement ? -1 : 1 ) );
                    x80_invoke_in( reg.c );
                    memory[ hl ] = reg.a;
                }
                else
                {
                    reg.a = memory[ hl ];
                    x80_invoke_out( reg.c );
                }
                reg.a = old_a;
                reg.SetH( decrement ? hl - 1 : hl + 1 );
                reg.b--;
                if ( !input )
                    reg.z80_set_memptr( reg.B() + ( decrement ? -1 : 1 ) );
                reg.fZero = ( 0 == reg.b );
                reg.fWasSubtract = true;
                cycles = 4;
            }
            else if ( 0xb2 == op2 || 0xba == op2 || // inir / indr
                      0xb3 == op2 || 0xbb == op2 )   // otir / otdr
            {
                bool input = ( 0 == ( op2 & 1 ) );
                bool decrement = ( 0 != ( op2 & 8 ) );
                uint16_t hl = reg.H();
                uint8_t old_a = reg.a;
                if ( input )
                {
                    reg.z80_set_memptr( reg.B() + ( decrement ? -1 : 1 ) );
                    x80_invoke_in( reg.c );
                    memory[ hl ] = reg.a;
                }
                else
                {
                    reg.a = memory[ hl ];
                    x80_invoke_out( reg.c );
                }
                reg.a = old_a;
                reg.SetH( decrement ? hl - 1 : hl + 1 );
                reg.b--;
                if ( !input )
                    reg.z80_set_memptr( reg.B() + ( decrement ? -1 : 1 ) );
                reg.fZero = ( 0 == reg.b );
                reg.fWasSubtract = true;
                if ( 0 != reg.b )
                {
                    cycles = 5;
                    pc -= 2;
                }
                else
                    cycles = 4;
            }
            else if ( 0x42 == ( op2 & 0xcf ) ) // sbc hl, rp AKA sbc hl, ss
            {
                cycles = 15;
                uint8_t rp = ( op2 >> 4 ) & 3;
                uint16_t val = ( 3 == rp ) ? sp : reg.rpValue( rp );
                uint16_t oldH = reg.H();
                reg.SetH( z80_op_sub_16( oldH, val, reg.fCarry ) );
                reg.z80_set_memptr( oldH + 1 );
            }
            else
                z80_ni( op, op2 );
            break;
        }
        default:
            z80_ni( op, op2 );
    }

    return cycles;
} //z80_emulate

void z80_renderByteReg( char * acfrom, uint8_t op, uint8_t fromval )
{
    memset( acfrom, 0, 4 );
    assert( 0xdd == op || 0xfd == op );
    assert( fromval <= 7 );
    if ( fromval < 4 )
        acfrom[ 0 ] = 'b' + fromval;
    else if ( 7 == fromval )
        acfrom[0] = 'a';
    else
        snprintf( acfrom, 4, "%s%c", 0xdd == op ? "ix" : "iy", 4 == fromval ? 'h' : 'l' );
} //z80_renderByteReg

void z80_render( char * ac, size_t bufferSize, uint8_t op, uint16_t address )
{
    uint8_t op2 = memory[ address + 1 ];
    uint8_t op3 = memory[ address + 2 ];
    uint8_t op4 = memory[ address + 3 ];
    uint16_t op34 = (uint16_t) op3 + ( (uint16_t) op4 << 8 );
    int16_t op2int = (int16_t) (int8_t) op2;
    int16_t op3int = (int16_t) (int8_t) op3;
    snprintf( ac, bufferSize, "z80 %02x %02x %02x NYI", op, op2, op3 );

    if ( 0xdd == op || 0xfd == op ) // ix & iy operations
    {
        const char * i = ( 0xdd == op ) ? "ix" : "iy";

        if ( 0x46 == ( op2 & 0xc7 ) ) // must include bit 7; see the matching fix in z80_emulate
        {
            uint8_t src = ( ( op2 >> 3 ) & 0x7 );
            snprintf( ac, bufferSize, "ld %s, (%s%s%d)", reg_strings[ src ], i, op3int >= 0 ? "+" : "", op3int );
        }
        else if ( 0x70 == ( op2 & 0xf8 ) )
        {
            uint8_t src = op2 & 0x7;
            if ( 6 == src )
                snprintf( ac, bufferSize, "ld (%s%s%d), %02x", i, op3int >= 0 ? "+" : "", op3int, op4 );
            else
                snprintf( ac, bufferSize, "ld (%s%s%d), %s", i, op3int >= 0 ? "+" : "", op3int, reg_strings[ src ] );
        }
        else if ( 0x09 == ( op2 & 0xcf ) )
            snprintf( ac, bufferSize, "add %s, %s", i, rp_strings[ ( op2 >> 4 ) & 0x3 ] );
        else if ( 0x21 == op2 )
            snprintf( ac, bufferSize, "ld %s, %04xh", i, op34 );
        else if ( 0x22 == op2 )
            snprintf( ac, bufferSize, "ld (%04xh), %s", op34, i );
        else if ( 0x23 == op2 )
            snprintf( ac, bufferSize, "inc %s", i );
        else if ( 0x26 == op2 )
            snprintf( ac, bufferSize, "ld %sh, %02x", i, op3 );
        else if ( 0x2a == op2 )
            snprintf( ac, bufferSize, "ld %s, (%04xh)", i, op34 );
        else if ( 0x2b == op2 )
            snprintf( ac, bufferSize, "dec %s", i );
        else if ( 0x2e == op2 )
            snprintf( ac, bufferSize, "ld %sl, %02x", i, op3 );
        else if ( 0x34 == op2 )
            snprintf( ac, bufferSize, "inc (%s%s%d)", i, op3int >= 0 ? "+" : "", op3int );
        else if ( 0x35 == op2 )
            snprintf( ac, bufferSize, "dec (%s%s%d)", i, op3int >= 0 ? "+" : "", op3int );
        else if ( 0x36 == op2 )
            snprintf( ac, bufferSize, "ld (%s%s%d), %02xh", i, op3int >= 0 ? "+" : "", op3int, op4 );
        else if ( ( op2 >= 0x40 && op2 <= 0x6f ) || ( op2 >= 0x78 && op2 <= 0x7f ) )
        {
            char acto[ 4 ] = {0};
            char acfrom[ 4 ] = {0};
            uint8_t fromval = op2 & 0xf;
            if ( fromval >= 8 )
                fromval -= 8;
            z80_renderByteReg( acfrom, op, fromval );

            if ( op2 <= 0x47 )
                acto[0] = 'b';
            else if ( op2 <= 0x4f )
                acto[0] = 'c';
            else if ( op2 <= 0x57 )
                acto[0] = 'd';
            else if ( op2 <= 0x5f )
                acto[0] = 'e';
            else if ( op2 <= 0x67 )
                snprintf( acto, _countof( acto ), "%sh", i );
            else if ( op2 <= 0x6f )
                snprintf( acto, _countof( acto ), "%sl", i );
            else
                acto[0] = 'a';

            snprintf( ac, bufferSize, "ld %s, %s", acto, acfrom );
        }
        else if ( 0x2a == op2 )
            snprintf( ac, bufferSize, "ld %s, (%04x)", i, op34 );
        else if ( 0x22 == op2 )
            snprintf( ac, bufferSize, "ld (%04x), %s", op34, i );
        else if ( 0x84 == ( op2 & 0xc6 ) ) // math on ixh/ixl/iyh/iyl with a; see the matching fix in z80_emulate
        {
            uint8_t math = ( ( op2 >> 3 ) & 0x7 );
            snprintf( ac, bufferSize, "%s a, %s%c", z80_math_strings[ math ], i, ( op2 & 1 ) ? 'l' : 'h' );
        }
        else if ( 0x86 == ( op2 & 0xc7 ) ) // math on [ ix/iy + index ]. 86, 8e, 96, 9e, a6, ae, b6, be
        {
            uint8_t math = ( ( op2 >> 3 ) & 0x7 );
            snprintf( ac, bufferSize, "%s a, (%s%s%d)", z80_math_strings[ math ], i, op3int >=0 ? "+" : "", op3int );
        }
        else if ( 0xcb == op2 && ( op4 <= 0x3f ) )
        {
            uint8_t rot = ( ( op4 >> 3 ) & 0x7 );
            int offset = (int) (int8_t) op3;
            snprintf( ac, bufferSize, "%s %s, (%s%s%d)", z80_rotate_strings[ rot ], reg_strings[ op4 & 0x7 ], i, offset >= 0 ? "+" : "", offset );
        }
        else if ( 0xcb == op2 && ( ( ( op4 & 0xf ) == 0xe ) || ( ( op4 & 0xf ) == 0x6 ) ) ) // bit operations on indexed memory
        {
            uint8_t index = op3;
            int32_t index32 = (int32_t) (int8_t) index;
            uint8_t mod = op4;
            uint8_t bit = ( mod >> 3 ) & 0x7;
            uint8_t top2bits = mod & 0xc0;

            if ( 0x40 == top2bits ) // bit
                snprintf( ac, bufferSize, "bit %u, (%s%s%d)", bit, i, index32 >= 0 ? "+" : "", index32 );
            else if ( 0x80 == top2bits ) // reset
                snprintf( ac, bufferSize, "res %u, (%s%s%d)", bit, i, index32 >= 0 ? "+" : "", index32 );
            else if ( 0xc0 == top2bits ) // set
                snprintf( ac, bufferSize, "set %u, (%s%s%d)", bit, i, index32 >= 0 ? "+" : "", index32 );
            else if ( 0x26 == op4 ) // sla
                snprintf( ac, bufferSize, "sla (%s%s%d)", i, index32 >= 0 ? "+" : "", index32 );
            else if ( 0x2e == op4 ) // sra
                snprintf( ac, bufferSize, "sra (%s%s%d)", i, index32 >= 0 ? "+" : "", index32 );
            else if ( 0x3e == op4 ) // srl
                snprintf( ac, bufferSize, "srl (%s%s%d)", i, index32 >= 0 ? "+" : "", index32 );
            else if ( 0x00 == top2bits ) // rlc/rl/rrc/rl rotate of (i + index)
            {
                const char * pc = "n/a";
                if ( 0x06 == op4 )
                    pc = "rlc";
                else if ( 0x0e == op4 )
                    pc = "rrc";
                else if ( 0x16 == op4 )
                    pc = "rl";
                else if ( 0x1e == op4 )
                    pc = "rr";
                snprintf( ac, bufferSize, "%s (%s%s%d)", pc, i, index32 >= 0 ? "+" : "", index32 );
            }
        }
        else if ( 0xe1 == op2 )
            snprintf( ac, bufferSize, "pop %s", i );
        else if ( 0xe3 == op2 )
            snprintf( ac, bufferSize, "ex (sp), %s", i );
        else if ( 0xe5 == op2 )
            snprintf( ac, bufferSize, "push %s", i );
        else if ( 0xe9 == op2 )
            snprintf( ac, bufferSize, "jp (%s)", i );    // this official syntax is bad; it's not indirect
        else if ( 0xf9 == op2 )
            snprintf( ac, bufferSize, "ld sp, %s", i );
        else
            snprintf( ac, bufferSize, "unknown 0xdd / 0xfd instruction, op2 %2x", op2 );
    }
    else if ( 0xed == op ) // load and compare operations
    {
        if ( 0xb == ( op2 & 0xf ) )
            snprintf( ac, bufferSize, "ld %s, (%04xh)", rp_strings[ ( ( op2 & 0xf0 ) >> 4 ) - 4 ], op34 );
        else if ( 0x3 == ( op2 & 0xf ) )
            snprintf( ac, bufferSize, "ld (%04xh), %s", op34, rp_strings[ ( ( op2 & 0xf0 ) >> 4 ) - 4 ] );
        else if ( 0x44 == op2 )
            strcpy( ac, "neg" );
        else if ( 0x47 == op2 )
            snprintf( ac, bufferSize, "ld i, a" );
        else if ( 0x4f == op2 )
            snprintf( ac, bufferSize, "ld r, a" );
        else if ( 0x57 == op2 )
            snprintf( ac, bufferSize, "ld a, i" );
        else if ( 0x5f == op2 )
            snprintf( ac, bufferSize, "ld a, r" );
        else if ( 0x67 == op2 )
            strcpy( ac, "rrd" );
        else if ( 0x6f == op2 )
            strcpy( ac, "rld" );
        else if ( 0xa0 == op2 )
            strcpy( ac, "ldi" );
        else if ( 0xa8 == op2 )
            strcpy( ac, "ldd" );
        else if ( 0xb0 == op2 )
            strcpy( ac, "ldir" );
        else if ( 0xb8 == op2 )
            strcpy( ac, "lddr" );
        else if ( 0x02 == ( op2 & 0x8f ) )
        {
            uint8_t rp = ( ( ( op2 & 0x30 ) >> 4 ) & 3 );
            snprintf( ac, bufferSize, "sbc hl, %s", rp_strings[ rp ] );
        }
        else if ( 0x4a == ( op2 & 0xcf ) ) // adc hl, rp
        {
            uint8_t rp = ( ( op2 >> 4 ) & 3 );
            snprintf( ac, bufferSize, "adc hl, %s", rp_strings[ rp ] );
        }
        else if ( 0xa1 == op2 )
            strcpy( ac, "cpi" );
        else if ( 0xa9 == op2 )
            strcpy( ac, "cpd" );
        else if ( 0xb1 == op2 )
            strcpy( ac, "cpir" );
        else if ( 0xb9 == op2 )
            strcpy( ac, "cpdr" );
    }
    else if ( 0xcb == op ) // rotate / bits. There is no sll in documented Z80
    {
        if ( 0x20 == ( op2 & 0xf8 ) ) // sla = shift left arithmetic
        {
            uint8_t rm = op2 & 0x7;
            snprintf( ac, bufferSize, "sla %s", reg_strings[ rm ] );
        }
        else if ( 0x28 == ( op2 & 0xf8 ) ) // sra = shift right arithmetic
        {
            uint8_t rm = op2 & 0x7;
            snprintf( ac, bufferSize, "sra %s", reg_strings[ rm ] );
        }
        else if ( op2 >= 0x30 && op2 <= 0x3f )
        {
            uint8_t rm = op2 & 0x7;
            if ( op2 <= 0x37 )
                snprintf( ac, bufferSize, "sll %s", reg_strings[ rm ] );
            else
                snprintf( ac, bufferSize, "srl %s", reg_strings[ rm ] );
        }
        else if ( 0x38 == ( op2 & 0xf8 ) ) // srl = shift right logical
        {
            uint8_t rm = op2 >> 4;
            snprintf( ac, bufferSize, "srl %s", reg_strings[ rm ] );
        }
        else if ( op2 <= 0x1f ) // rlc, rl, rrc, rr on rm
        {
            const char * rotateType = "rlc";
            if ( op2 >= 0x10 && op2 <= 0x17 )
                rotateType = "rl";
            else if ( op2 >= 0x08 && op2 <= 0x0f )
                rotateType = "rrc";
            else if ( op2 >= 0x18 && op2 <= 0x1f )
                rotateType = "rr";

            uint8_t rm = ( op2 & 0x7 );
            snprintf( ac, bufferSize, "%s %s", rotateType, reg_strings[ rm ] );
        }
        else if ( op2 >= 0x40 && op2 <= 0x7f ) // bit #, rm
        {
            uint8_t rm = op2 & 0x7;
            uint8_t bit = ( op2 >> 3 ) & 0x7;
            snprintf( ac, bufferSize, "bit %u, %s", bit, reg_strings[ rm ] );
        }
        else if ( op2 >= 0x80 && op2 <= 0xbf ) // res bit #, rm
        {
            uint8_t rm = op2 & 0x7;
            uint8_t bit = ( op2 >> 3 ) & 0x7;
            snprintf( ac, bufferSize, "res %u, %s", bit, reg_strings[ rm ] );
        }
        else if ( op2 >= 0xc0 && op2 <= 0xff ) // set bit #, rm
        {
            uint8_t rm = op2 & 0x7;
            uint8_t bit = ( op2 >> 3 ) & 0x7;
            snprintf( ac, bufferSize, "set %u, %s", bit, reg_strings[ rm ] );
        }
    }
    else if ( 0x08 == op )
        strcpy( ac, "ex af,af'" );
    else if ( 0x10 == op )
        snprintf( ac, bufferSize, "djnz %d", op2int );
    else if ( 0x18 == op )
        snprintf( ac, bufferSize, "jr %d", op2int );
    else if ( 0x20 == op )
        snprintf( ac, bufferSize, "jr nz, %d", op2int );
    else if ( 0x28 == op )
        snprintf( ac, bufferSize, "jr z, %d", op2int );
    else if ( 0x30 == op )
        snprintf( ac, bufferSize, "jr nc, %d", op2int );
    else if ( 0x38 == op )
        snprintf( ac, bufferSize, "jr c, %d", op2int );
    else if ( 0xd9 == op )
        strcpy( ac, "exx" );
    else
        strcpy( ac, "unknown instruction" );
} //z80_render

void replace_with_num( char * pc, const char * psearch, uint16_t num, uint8_t width )
{
    char actemp[ 60 ];
    snprintf( actemp, _countof( actemp ), ( 16 == width ) ? "%04xh" : "%02xh", (uint32_t) num );
    strcat( actemp, pc + strlen( psearch ) );
    strcpy( pc, actemp );
} //replace_with_num

const char * x80_render_operation( uint16_t address )
{
    static char ac[ 128 ];
    uint8_t op = memory[ address ];
    bool renderData = true;

    strcpy( ac, z80_instructions[ op ] );
    if ( '*' == ac[ 0 ] )
    {
        z80_render( ac, _countof( ac ), op, address );
        renderData = false;
    }

    if ( renderData )
    {
        char * p;
        if ( ( p = strstr( ac, "d16" ) ) )
            replace_with_num( p, "d16", mword( address + 1 ), 16 );
        else if ( ( p = strstr( ac, "a16" ) ) )
            replace_with_num( p, "a16", mword( address + 1 ), 16 );
        else if ( ( p = strstr( ac, "d8" ) ) )
            replace_with_num( p, "d8", memory[ address + 1 ], 8 );
    }

    return ac;
} //x80_render_operation

uint8_t x80_instruction_length( uint16_t address )
{
    uint8_t op = memory[ address ];
    const char * instruction;

    if ( op == 0xcb )
        return 2;
    if ( op == 0xed )
    {
        uint8_t op2 = memory[ (uint16_t) ( address + 1 ) ];
        return ( op2 & 0x0f ) == 0x0b || ( op2 & 0x0f ) == 0x03 ? 4 : 2;
    }
    if ( op == 0xdd || op == 0xfd )
    {
        uint8_t op2 = memory[ (uint16_t) ( address + 1 ) ];
        if ( op2 == 0xcb || op2 == 0x21 || op2 == 0x22 || op2 == 0x2a || op2 == 0x36 )
            return 4;
        if ( ( op2 & 0x47 ) == 0x46 || ( op2 & 0xf8 ) == 0x70 ||
             ( op2 & 0xc7 ) == 0x86 || op2 == 0x34 || op2 == 0x35 ||
             op2 == 0x26 || op2 == 0x2e )
            return 3;
        return 2;
    }

    instruction = z80_instructions[ op ];
    if ( strstr( instruction, "d16" ) || strstr( instruction, "a16" ) )
        return 3;
    if ( strstr( instruction, "d8" ) || op == 0x10 || op == 0x18 ||
         op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38 )
        return 2;
    return 1;
} //x80_instruction_length

not_inlined void x80_trace_state()
{
    if ( !tracer.IsEnabled() ) // trace instructions may be on but global tracing turned off
        return;

    uint8_t op = memory[ reg.pc ];
    uint8_t op2 = memory[ reg.pc + 1 ];
    uint8_t op3 = memory[ reg.pc + 2 ];
    uint8_t op4 = memory[ reg.pc + 3 ];

    tracer.Trace( "pc %04x, op %02x, op2 %02x, op3 %02x, op4 %02x, a %02x, B %04x, D %04x, H %04x, ix %04x, iy %04x, sp %04x, %s, %s\n",
                  reg.pc, op, op2, op3, op4, reg.a, reg.B(), reg.D(), reg.H(), reg.ix, reg.iy, reg.sp,
                  reg.renderFlags(), x80_render_operation( reg.pc ) );

//    tracer.TraceBinaryData( & memory[ 0x42c5 + 0x16 ], 2, 4 );
} //x80_trace_state

bool check_conditional( uint8_t op ) // checks for conditional jump, call, and return
{
    bool condition = reg.getFlag( ( op >> 4 ) & 3 );
    if ( ( 0 == ( op & 8 ) ) )
        condition = !condition;

    return condition;
} //check_conditional

not_inlined bool handle_state() // this code exists to reduce what would be multiple checks per instruction loop to just one check
{
    if ( g_State & stateEndEmulation )
        return true;

    if ( g_State & stateTraceInstructions )
        x80_trace_state();

    return false;
} //handle_state

// Keep cycle-accurate stepping without charging production instruction batches
// for cycle-table loads and cycle-limit checks on every emulated instruction.
template<bool track_cycles>
static uint32_t x80_emulate_budget( uint32_t maxcycles, uint16_t maxinstructions )
{
    uint32_t cycles = 0;
    uint32_t instructions_remaining = maxinstructions;
    uint16_t pc = reg.pc;
    uint16_t sp = reg.sp;
    uint16_t last_sp_before = sp;
    uint8_t op = OPCODE_NOP;
    const acycles_t & acycles = z80_cycles;

    // with the Watcom compiler for real-mode DOS, the cycle check, cycle addition, and trace check consume 17% of runtime

    while ( ( !track_cycles || cycles < maxcycles ) && instructions_remaining != 0 )
    {
#ifndef ESP_PLATFORM
        if ( 0 != g_State )
        {
            reg.pc = pc;
            reg.sp = sp;
            if ( handle_state() )
                break;
            pc = reg.pc;
            sp = reg.sp;
        }
#endif

        op = memory[ pc ];              // 1% of runtime
        pc++;                           // 7% of runtime
        if constexpr ( track_cycles )
            cycles += acycles[ op ];
        instructions_remaining--;

        switch ( op )                   // 50% of runtime is completing cycle addition & setting up for the jump table jump
        {
            case 0x00: break; // nop
            case 0x01: case 0x11: case 0x21: { reg.setRpValueFromOp( op, pcword( pc ) ); break; } // lxi bc/de/hl, d16
            case 0x31: { sp = pcword( pc ); break; } // lxi sp, d16
            case 0x02: { memory[ reg.B() ] = reg.a; reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) | ( ( reg.B() + 1 ) & 0xff ) ); break; } // stax b
            case 0x03: case 0x13: case 0x23: // inx bc/de/hl. no status flag updates
            {
                uint8_t rp = ( op >> 4 ) & 3;
                reg.setRpValue( rp, reg.rpValue( rp ) + 1 );
                break;
            }
            case 0x33: { sp++; break; } // inx sp
            case 0x04: case 0x14: case 0x24: case 0x34: case 0x0c: case 0x1c: case 0x2c: case 0x3c: // inr rm. does not set carry
            {
                uint8_t * pdst = dst_address( op );
                *pdst = op_inc( *pdst );
                break;
            }
            case 0x05: case 0x15: case 0x25: case 0x35: case 0x0d: case 0x1d: case 0x2d: case 0x3d: // dcr rm. does not set carry
            {
                uint8_t * pdst = dst_address( op );
                *pdst = op_dec( * pdst );          // 5% of runtime
                break;
            }
            case 0x06: case 0x16: case 0x26: case 0x36: case 0x0e: case 0x1e: case 0x2e: case 0x3e: // mvi rm, d8
            {
                * dst_address( op ) = pcbyte( pc );
                break;
            }
            case 0x07: // rlc
            {
                reg.fCarry = ( 0 != ( reg.a & 0x80 ) );
                reg.a <<= 1;
                if ( reg.fCarry )
                    reg.a |= 1;
                reg.clearHN();
                reg.z80_assignYX( reg.a );
                break;
            }
            case 0x09: case 0x19: case 0x29: { op_dad( reg.rpValueFromOp( op ) ); break; } // dad bc/de/hl
            case 0x39: { op_dad( sp ); break; } // dad sp
            case 0x0a: { reg.z80_set_memptr( reg.B() + 1 ); reg.a = memory[ reg.B() ]; break; } // ldax b
            case 0x0b: case 0x1b: case 0x2b: // dcx bc/de/hl. no status flag updates
            {
                uint8_t rp = ( op >> 4 ) & 3;
                reg.setRpValue( rp, reg.rpValue( rp ) - 1 );
                break;
            }
            case 0x3b: { sp--; break; } // dcx sp
            case 0x0f: // rrc
            {
                reg.fCarry = ( 0 != ( reg.a & 1 ) );
                reg.a >>= 1;
                if ( reg.fCarry )
                    reg.a |= 0x80;
                reg.clearHN();
                reg.z80_assignYX( reg.a );
                break;
            }
            case 0x12: { memory[ reg.D() ] = reg.a; reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) | ( ( reg.D() + 1 ) & 0xff ) ); break; } // stax d
            case 0x17: // ral
            {
                bool c = reg.fCarry;
                reg.fCarry = ( 0 != ( 0x80 & reg.a ) );
                reg.a <<= 1;
                if ( c )
                    reg.a |= 1;
                reg.clearHN();
                reg.z80_assignYX( reg.a );
                break;
            }
            case 0x1a: { reg.z80_set_memptr( reg.D() + 1 ); reg.a = memory[ reg.D() ]; break; } // ldax d
            case 0x1f: // rar
            {
                bool c = reg.fCarry;
                reg.fCarry = ( 0 != ( reg.a & 1 ) );
                reg.a >>= 1;
                if ( c )
                    reg.a |= 0x80;
                reg.clearHN();
                reg.z80_assignYX( reg.a );
                break;
            }
            case 0x22: { uint16_t addr = pcword( pc ); setmword( addr, reg.H() ); reg.z80_set_memptr( addr + 1 ); break; } // shld
            case 0x27: { op_daa(); break; } // daa
            case 0x2a: { uint16_t addr = pcword( pc ); reg.SetH( mword( addr ) ); reg.z80_set_memptr( addr + 1 ); break; } // lhld
            case 0x2f: { op_cma(); break; } // cma
            case 0x32: { uint16_t addr = pcword( pc ); memory[ addr ] = reg.a; reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) | ( ( addr + 1 ) & 0xff ) ); break; } // sta a16
            case 0x37: // stc
            {
                reg.fCarry = 1;
                reg.clearHN();
                reg.z80_assignYX( reg.a );
                break;
            }
            case 0x3a: { uint16_t addr = pcword( pc ); reg.a = memory[ addr ]; reg.z80_set_memptr( addr + 1 ); break; } // lda a16
            case 0x3f: { op_cmc(); break; } // cmc
            case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: // mov
            case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
            case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            case 0x60: case 0x61: case 0x62: case 0x63:            case 0x65: case 0x66: case 0x67:
            case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
            case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75:            case 0x77:
            case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            {
                * dst_address( op ) = src_value( op );
                break;
            }
            case 0x64: // hook
            {
                reg.pc = pc;
                reg.sp = sp;
                op = x80_invoke_hook();
                pc = reg.pc;
                sp = reg.sp;
                if constexpr ( track_cycles )
                    cycles += acycles[ op ];
                if ( OPCODE_HLT == op ) // treat each possible return opcode separately to avoid a jump to restart for performance
                    goto _op_hlt;
                else if ( OPCODE_NOP == op )
                    break;
                else if ( OPCODE_RET == op )
                    goto _op_ret;
                else
                    assert( false );
            }
            case 0x76: { _op_hlt: x80_invoke_halt(); goto _all_done; } // hlt
            case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: { op_add( src_value( op ) ); break; }
            case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8c: case 0x8d: case 0x8e: case 0x8f: { op_adc( src_value( op ) ); break; }
            case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: { reg.a = op_sub( src_value( op ) ); break; }
            case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: case 0x9f: { op_sbb( src_value( op ) ); break; }
            case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xa6: case 0xa7: { op_ana( src_value( op ) ); break; }
            case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf: { op_xra( src_value( op ) ); break; }
            case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7: { op_ora( src_value( op ) ); break; }
            case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf: { op_cmp( src_value( op ) ); break; }
            case 0xc0: case 0xd0: case 0xe0: case 0xf0: case 0xc8: case 0xd8: case 0xe8: case 0xf8: // conditional return
            {
                last_sp_before = sp;
                if ( check_conditional( op ) )
                {
                    pc = popword( sp );
                    reg.z80_set_memptr( pc );
                }
                else
                    if constexpr ( track_cycles )
                        cycles -= cyclesnt;
                break;
            }
            case 0xc1: case 0xd1: case 0xe1: { last_sp_before = sp; reg.setRpValueFromOp( op, popword( sp ) ); break; } // pop rp
            case 0xc2: case 0xd2: case 0xe2: case 0xf2: case 0xca: case 0xda: case 0xea: case 0xfa: // conditional jmp
            {
                uint16_t address = pcword( pc ); // must be consumed regardless of whether jump is taken
                reg.z80_set_memptr( address );
                if ( check_conditional( op ) )
                    pc = address;
                else
                    if constexpr ( track_cycles )
                        cycles -= cyclesnt;
                break;
            }
            case 0xc3: { uint16_t address = pcword( pc ); reg.z80_set_memptr( address ); pc = address; break; } // jmp a16
            case 0xc4: case 0xd4: case 0xe4: case 0xf4: case 0xcc: case 0xdc: case 0xec: case 0xfc: // conditional call
            {
                last_sp_before = sp;
                uint16_t address = pcword( pc ); // must be consumed regardless of whether call is taken
                reg.z80_set_memptr( address );
                if ( check_conditional( op ) )
                {
                    pushword( sp, pc );
                    pc = address;
                }
                else
                    if constexpr ( track_cycles )
                        cycles -= cyclesnt;
                break;
            }
            case 0xc5: case 0xd5: case 0xe5: { last_sp_before = sp; pushword( sp, reg.rpValueFromOp( op ) ); break; } // push rp
            case 0xc6: { op_add( pcbyte( pc ) ); break; } // adi
            case 0xc7: case 0xd7: case 0xe7: case 0xf7: case 0xcf: case 0xdf: case 0xef: case 0xff: // rst
            {
                last_sp_before = sp;
                // bits 5..3 are exp, which form an address 0000000000exp000 that is called.
                // rst is generally invoked by DDT and hardware interrupts, which supply the one instruction rst.

                pushword( sp, pc );
                pc = 0x38 & (uint16_t) op;
                reg.z80_set_memptr( pc );
                break;
            }
            case 0xc9: { _op_ret: last_sp_before = sp; pc = popword( sp ); reg.z80_set_memptr( pc ); break; } // ret
            case 0xcd: { last_sp_before = sp; uint16_t t = pcword( pc ); reg.z80_set_memptr( t ); pushword( sp, pc ); pc = t; break; } // call a16
            case 0xce: { op_adc( pcbyte( pc ) ); break; } // aci
            case 0xd3: { uint8_t port = pcbyte( pc ); x80_invoke_out( port ); reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) | ( ( port + 1 ) & 0xff ) ); break; } // out d8
            case 0xd6: { reg.a = op_sub( pcbyte( pc ) ); break; } // sui
            case 0xdb: { uint8_t port = pcbyte( pc ); reg.z80_set_memptr( ( (uint16_t) reg.a << 8 ) + port + 1 ); x80_invoke_in( port ); break; } // in d8
            case 0xde: { op_sbb( pcbyte( pc ) ); break; } // sbi
            case 0xe3: { uint16_t t = reg.H(); reg.SetH( mword( sp ) ); setmword( sp, t ); reg.z80_set_memptr( reg.H() ); break; } // xthl
            case 0xe6: { op_ana( pcbyte( pc ) ); break; } // ani
            case 0xe9: { pc = reg.H(); break; } // pchl
            case 0xeb: { uint16_t t = reg.H(); reg.SetH( reg.D() ); reg.SetD( t ); break; } // xchg
            case 0xee: { op_xra( pcbyte( pc ) ); break; } // xri
            case 0xf1: { last_sp_before = sp; reg.SetPSW( popword( sp ) ); break; } // pop psw
            case 0xf3: { reg.fINTE = false; reg.fINTE2 = false; break; } // di
            case 0xf5: { last_sp_before = sp; pushword( sp, reg.PSW() ); break; } // push psw
            case 0xf6: { op_ora( pcbyte( pc ) ); break; } // ori
            case 0xf9: { sp = reg.H(); break; } // sphl
            case 0xfb: { reg.fINTE = true; reg.fINTE2 = true; break; } // ei
            case 0xfe: { op_cmp( pcbyte( pc ) ); break; } // cpi
            default:
            {
                last_sp_before = sp;
                if constexpr ( track_cycles )
                    cycles += z80_emulate( op, pc, sp );
                else
                    z80_emulate( op, pc, sp );
            } //default
        } //switch

        reg.z80_increment_r();
    } //while
_all_done:
    reg.pc = pc;
    reg.sp = sp;
    x80_last_sp_before = last_sp_before;
    x80_last_opcode = op;
    x80_last_io_status = op == 0xdb ? X80_IO_INPUT
                       : op == 0xd3 ? X80_IO_OUTPUT
                       : X80_IO_NONE;
    return cycles;
} //x80_emulate

uint16_t x80_emulate( uint16_t maxcycles )
{
    return (uint16_t)x80_emulate_budget<true>( maxcycles, UINT16_MAX );
}

X80_HOT_CODE void x80_emulate_instructions( uint16_t maxinstructions )
{
    x80_emulate_budget<false>( 0, maxinstructions );
}
