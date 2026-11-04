; ;@var loc= with a symbol: a label, label+N, a %define'd number or label; the rest stays as written
;@lang c
%define var_total 0x00000078
%define var_hi 0xFFFFFFF0
%define fptr _target
%define fptr2 _target+0x2
__function_main:
    push BP
    mov BP, SP
;@src s.c:1
    mov R0, 1
;@var a loc=_target type=int
;@var b loc=_target+2 type=int
;@var c loc=_target-1 type=int
;@var d loc=var_total type=int
;@var e loc=var_total+3 type=int
;@var f loc=fptr
;@var g loc=fptr2
;@var h loc=var_hi
;@var i loc=_data+1
;@var j loc=BP-3
;@var k loc=R5
;@var l loc=@0x20003000
;@var m loc=-
;@var n loc=_nowhere
;@var o loc=_nowhere+1
;@var p loc=bp+1
;@var q optimized-out
    mov R1, 2
    ret
_target:
    mov R2, 3
    mov R3, 4
_data:
    integer 7
