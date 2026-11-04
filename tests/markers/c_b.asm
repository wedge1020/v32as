;@lang c
%define var_total 0x00000041
global __function_cb
__function_cb:
;@src b.c:1
    push BP
;@var arg loc=BP+3 type=int
;@var total loc=var_total type=int
;@var self loc=_shared type=int
;@var lb loc=_local_b+1 type=int
    mov R0, 2
    pop BP
    ret
global _callback
_callback:
    mov R1, 1
    ret
global _shared
_shared:
    integer 1
    integer 2
_local_b:
    integer 6
