_main:
  mov R0, _table+2
  call _helper
  mov R1, [_local+1]
  mov R2, IP
  hlt
_local:
  integer 7, 8
  pointer _helper, _local, _table+1
_helper:
  ret
_table:
  integer 1,2,3
