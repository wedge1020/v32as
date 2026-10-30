; ;@var / ;@scope semantics
%define R_ARGS 1
__function_outer:
    push BP
    mov BP, SP
;@var p loc=BP+2 type=lua
;@var q loc=BP+3 type=int boxed
;@src o.lua:1
    mov R0, 1
;@scope {
;@var t loc=BP-1
;@src o.lua:2
    mov R1, 2
; the same name in the same block: a new home (the optimizer's) ends the old range
;@var t loc=R5
    mov R2, 3
; a shadow in a nested block does not end the outer one
;@scope {
;@var p loc=BP-2 type=lua
;@src o.lua:3
    mov R3, 4
;@scope }
;@src o.lua:4
    mov R4, 5
;@var t optimized-out
    mov R5, 6
;@scope }
;@src o.lua:5
    mov R6, 7
; a nested function inside the parent's code; the parent goes on after it
    jmp __inner_skip
__function_inner:
;@src o.lua:8
    push BP
;@var x loc=BP+2 type=lua
    mov R0, 8
    ret
;@fn __function_inner escapes=none
__inner_skip:
;@src o.lua:9
    mov R1, 9
;@fn __function_outer escapes=none
;@src -
    hlt
; after ;@src - everything is closed: this one has no function
;@var late loc=BP-1
;@src o.lua:10
    mov R2, 10
;@scope }
;@scope {
;@scope {
;@var open loc=BP-4 flag1 key=ignored flag2
    mov R3, 11
