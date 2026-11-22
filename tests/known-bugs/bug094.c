/* BUG-094: unreachable float constant division by zero rejected.
 * Float seeds 77632 and 82776. The divisions by zero never execute. */
#include <stdint.h>
#include <stdio.h>
static uint16_t a(void) { if (0) { if (0.001f / 0.0f) { return 2; } } return 1; }
static uint16_t control_nonzero(void) { if (0) { if (0.001f / 1.0f) { return 2; } } return 1; }
static uint16_t control_integer(void) { if (0) { if (5 / 0) { return 2; } } return 1; }
static uint16_t control_variable(void) { if (0) { float d=0.0f; if (0.001f / d) { return 2; } } return 1; }
int main(void) {
 printf("float zero %u\n",a());
 printf("nonzero control %u\n",control_nonzero());
 printf("integer control %u\n",control_integer());
 printf("variable control %u\n",control_variable());
 return 0;
}
