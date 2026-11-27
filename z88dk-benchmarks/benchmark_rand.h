/* Match dcc's 16-bit xorshift sequence for identical string workloads.
 * ZSDCC uses the same instruction sequence as DCCRTL.MAC's rand.
 * sccz80 keeps the equivalent C implementation for its calling convention.
 */
#ifndef BENCHMARK_RAND_H
#define BENCHMARK_RAND_H
unsigned int benchmark_rand_state = 1;
#ifdef SDCC
void benchmark_srand(unsigned int seed) __z88dk_fastcall __naked
{
#asm
    ld (_benchmark_rand_state),hl
    ret
#endasm
}
int benchmark_rand(void) __naked
{
#asm
    ld hl,(_benchmark_rand_state)
    ld a,h
    rra
    ld a,l
    rra
    xor h
    ld h,a
    ld a,l
    rra
    ld a,h
    rra
    xor l
    ld l,a
    xor h
    ld h,a
    ld (_benchmark_rand_state),hl
    res 7,h
    ret
#endasm
}
#else
void benchmark_srand(unsigned int seed) { benchmark_rand_state = seed; }
int benchmark_rand(void)
{
    benchmark_rand_state ^= benchmark_rand_state << 7;
    benchmark_rand_state ^= benchmark_rand_state >> 9;
    benchmark_rand_state ^= benchmark_rand_state << 8;
    return benchmark_rand_state & 0x7fff;
}
#endif
#ifdef srand
#undef srand
#endif
#ifdef rand
#undef rand
#endif
#define srand benchmark_srand
#define rand benchmark_rand
#endif
