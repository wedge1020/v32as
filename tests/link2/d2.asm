datasym __data_base
global _f
_f:
  mov R2, [__data_base+9]
  mov R3, _f
  ret
