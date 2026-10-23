%assign N 3
%assign N N * 2
%assign F 1.5
%assign F F * 2
%assign HEX 0xFF
%eval ALIAS N + 1               ; %eval NAME expr is the same as %assign
  mov R0, N
  mov R1, ALIAS
  float F
  mov R2, HEX
  mov R3, __COUNTER__
  mov R3, __COUNTER__
  mov R3, __COUNTER__
  mov R4, __LINE__
  string __FILE__
  mov R5, __INCLUDE_LEVEL__
  mov R6, __BASE__
%ifdef __CART__
  mov R7, 1
%endif
%ifdef __BIOS__
  mov R7, 2
%endif
%if __V32AS__ > 20260000
  mov R8, 1
%endif
