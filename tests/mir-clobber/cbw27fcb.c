#include <stdio.h>

volatile int w27_keep_callback_bodies;

void w27_callback_body(void) { printf("h\n"); }

#asm
    public _h1
    public _h2
    public _h3
_h1:
    jp _w27_callback_body
_h2:
    jp _w27_callback_body
_h3:
    jp _w27_callback_body
#endasm
