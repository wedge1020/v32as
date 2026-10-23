%ifdef NOPE
%frobnicate this is not a directive
%include "does_not_exist.inc"
%error "never"
%define X 1
  this is not even valid assembly @@@
%endif
%ifdef X
  mov R0, 1
%endif
  mov R1, 2
