; C-style producer: ;@lang c, three functions, no ";@src -" and no ";@fn" after the bodies:
; a new top-level __function_NAME: label ends the previous function
;@lang c
%define var_counter 0x00000010
;@src -
    call __function_main
    hlt
__function_main:
;@src t.c:3
    push BP
    mov BP, SP
;@var argc loc=BP+2 type=int
;@var counter loc=var_counter type=int
;@scope {
;@var i loc=BP-1 type=int
;@src t.c:4
    mov R0, 0
;@scope {
;@var tmp loc=R5
;@src t.c:5
    mov R5, 1
;@scope }
;@src t.c:6
    mov R1, 2
;@scope }
;@src t.c:7
    pop BP
    ret
; ";@fn" above a label is only a hint, for every language
;@fn __function_helper clobbers=R1
__function_helper:
;@src t.c:10
    push BP
    mov BP, SP
;@var x loc=BP+2 type=float
;@var y loc=BP+3 type=float
;@scope {
;@var z loc=R3 type=float
;@src t.c:11
    mov R3, 3
; the block is never closed: the next function's label ends it
__function_last:
;@src t.c:20
    mov R0, 9
;@var n loc=_helper_data+1 type=int
    mov R1, 9
    ret
_helper_data:
    integer 1
    integer 2
