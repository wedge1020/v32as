  mov R0, _tab+2
  mov R1, [_tab+3]
  mov [_tab-1], R1
  jmp _end+0
  mov R2, IP
_tab:
  integer 1, 2, 3, 4
  pointer _tab+1, _end, _tab-1
_end:
  hlt
