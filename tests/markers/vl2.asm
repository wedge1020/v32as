extern __function_f
global __function_g
__function_g:
    push BP
;@var b loc=BP+2 type=lua
;@src g.lua:1
    call __function_f
;@src g.lua:2
    mov R2, 3
    pop BP
    ret
;@fn __function_g escapes=none
