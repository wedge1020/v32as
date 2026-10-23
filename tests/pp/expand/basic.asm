%define SELF SELF + 1
%define A B
%define B A
%define NUM 10
%ifdef NUM
  mov R0, NUM       ; comment NUM
  mov R1, SELF
  mov R2, A
%endif
