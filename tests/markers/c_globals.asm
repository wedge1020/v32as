;@lang c
;@src -
;@var g1 loc=@0x00000005 type=int
;@var g2 loc=global_g2 type=float
%define global_g2 7
  call __function_main
  hlt
__function_main:
;@src t.c:3
  push BP
  mov BP, SP
;@var x loc=BP-1 type=int
  mov R0, 1
  pop BP
  ret
;@src -
__function_f:
;@src t.c:9
  mov R0, 2
  ret
;@src -
