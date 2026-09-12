#asm
    public _w28fa
    public _w28fr
    extrn _malloc
    extrn _free
_w28fa:
    push hl
    call _malloc
    pop bc
    ret
_w28fr:
    push hl
    call _free
    pop bc
    ret
#endasm
