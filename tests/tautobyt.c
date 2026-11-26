/* Automatic byte-helper substitution must preserve conversions, evaluation
 * counts, callbacks, volatile arguments, and distinct memory snapshots. */
#include <stdio.h>
static unsigned char hash(unsigned int x)
{ return (unsigned char)((x & 255U) ^ (x >> 8) ^ 90U); }
static volatile unsigned int watched;
static unsigned int global;
static int effects;
static unsigned int effect(void) { ++effects; return 0x1234U; }
static unsigned int shared(unsigned int x)
{ unsigned int word=(x & 255U) ^ (x >> 8); return (unsigned char)(word^90U)+(word<<8); }
static unsigned char memory(unsigned int x)
{ return (unsigned char)((x & 255U) ^ (watched >> 8) ^ 90U); }
static unsigned char snapshot(void)
{ unsigned int low=global & 255U; global=0xabcdU; return (unsigned char)(low ^ (global>>8) ^ 90U); }
static unsigned char mask(unsigned int x)
{ return (unsigned char)((x & 0xf1ffU) ^ (x >> 8) ^ 0xbeefU); }
static unsigned char (*callback)(unsigned int) = hash;
int main(void)
{
    unsigned int x=0x1234U;
    int signed_x=-32768;
    watched=0xabcdU;
    if (hash(x)!=124U || hash(x++)!=124U || x!=0x1235U ||
        hash(effect())!=124U || effects!=1 || hash(watched)!=60U ||
        hash(signed_x)!=218U || hash(0x12345678UL)!=116U ||
        callback(0x1234U)!=124U || hash(hash(0x1234U))!=38U ||
        shared(0x1234U)!=0x267cU || memory(0x1234U)!=197U || mask(0x1234U)!=201U) {
        puts("FAIL auto byte"); return 1;
    }
    x=0;
    do {
        unsigned char result=hash(x);
        unsigned int want=(x % 256U) ^ (x / 256U) ^ 90U;
        if(result!=want) { puts("FAIL exhaustive inline byte"); return 1; }
    } while(++x!=0);
    global=0x1234U;
    if (snapshot()!=197U) { puts("FAIL snapshot"); return 1; }
    puts("tautobyt passed"); return 0;
}
