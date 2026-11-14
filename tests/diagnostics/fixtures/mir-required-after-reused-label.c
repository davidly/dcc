/* Reusing a function-scoped label in a nested block must not hide a later
 * required-MIR emission failure. BUG-020 supplies a valid but unsupported
 * release shape; full-debug selection can emit it. The host stdout baseline
 * proves the fixture, while the Python matrix checks rejection diagnostics.
 * DCC-EXPECT-ERROR: MIR emission is required
 */
#include <stdint.h>
#include <stdio.h>
static int16_t sel=5, g16=3; static uint16_t hits;
static uint16_t first(void) { L1:; return 1; }
static uint16_t second(void) { { L1:; } return 2; }
static int32_t third(int32_t x) { switch(sel) { case 5: hits++; } if(g16 >= (x % 115)) return 1; return 0; }
int main(void) { printf("labels %u %u\n", (unsigned)first(), (unsigned)second()); printf("third %ld %ld\n", (long)third(70000), (long)third(-70000)); return 0; }
