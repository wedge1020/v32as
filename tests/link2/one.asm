global _main
_main:
  mov R0, 1
  call _g
  hlt
global _g
_g:
  mov R1, 2
  ret
