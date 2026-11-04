;@lang c
%define var_gcount 0x00000040
global __function_main
__function_main:
;@src a.c:1
    push BP
    mov BP, SP
;@var v loc=BP+2 type=int
;@var gcount loc=var_gcount type=int
;@var cb loc=_callback type=ptr
;@var sh loc=_shared+1 type=int
;@var lo loc=_local_a type=int
;@scope {
;@var w loc=R2
;@src a.c:2
    call __function_other
;@scope }
    pop BP
    ret
global __function_other
__function_other:
;@src a.c:7
    mov R0, 1
    ret
_local_a:
    integer 5
