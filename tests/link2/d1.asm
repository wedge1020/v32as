datasym __data_base
global _main
extern _f
_main:
  call _f
  mov R0, [__data_base+3]
  mov R1, IP
  hlt
