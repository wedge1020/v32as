%define LEVEL 2
%define FLAG
%if LEVEL == 1
  mov R0, 1
%elif LEVEL == 2 && defined(FLAG)
  mov R0, 2
%elif 1/0                      ; never evaluated: an earlier branch was taken
  mov R0, 3
%else
  mov R0, 4
%endif
%if 0
  %if 1/0                      ; dead blocks are not even evaluated
  %endif
  mov R1, 100
%elif defined LEVEL
  mov R1, 5
%endif
%ifdef NOPE
  mov R2, 1
%elifdef LEVEL
  mov R2, 6
%elifndef NOPE
  mov R2, 7
%endif
%ifndef NOPE
%elifdef LEVEL
  mov R3, 1
%else
  mov R3, 2
%endif
%if (LEVEL * 2.5) > 4.9 && 'A' == 65
  mov R4, 8
%endif
%if LEVEL
  %if !defined(OTHER)
    %define OTHER 9
  %endif
  mov R5, OTHER
%endif
