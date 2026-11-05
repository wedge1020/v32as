;@lang c
;@src -
    call __function_main
    hlt
__function_main:
;@src t.c:3
    push BP
    mov BP, SP
;@var i loc=BP-1 type=int
;@var s loc=BP-2 type=int
    mov R0, 1
;@home BP-1 R5
;@home BP-2 R4
    mov R5, 2
    mov R4, 3
;@var j loc=R5 type=int
    mov R1, 5
;@home R5 BP-1
;@home R4 optimized-out
    mov R2, 6
    pop BP
    ret
;@src -
