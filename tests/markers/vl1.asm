global __function_f
__function_f:
    push BP
;@var a loc=BP+2 type=lua
;@src f.lua:1
    mov R0, 1
;@scope {
;@var t loc=BP-1 type=lua
;@src f.lua:2
    mov R1, 2
;@scope }
    pop BP
    ret
;@fn __function_f escapes=none
