; startup code, no marker: no entry
    mov R0, 0
;@lang lua
;@src bench.lua:1
__function_main:
    mov R1, 2
    iadd R1, R0
;@src bench.lua:3
    mov R2, 3
    mov R3, 4
;@src bench.lua:3
    mov R4, 5
;@src -
__runtime_helper:
    mov R5, 6
;@src other.lua:10:7
__function_f__l0:
    ; the label may also come before the marker
    mov R6, 7
    hlt
