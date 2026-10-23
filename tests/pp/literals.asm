%define X 9
%define FFh 5         ; FFh is a hexadecimal NUMBER to the scanner, so this is never used
_msg:
  string "X ; X"      ; X in a comment
  mov R0, 'X'
  mov R1, X
  mov R2, FFh
  mov R3, #X
  mov R4, 0x1F
  float 1.5
