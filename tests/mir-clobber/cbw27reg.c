#asm
    public _w27_atexit
    extrn _atexit
_w27_atexit:
    push hl
    call _atexit
    pop bc
    ret
#endasm
