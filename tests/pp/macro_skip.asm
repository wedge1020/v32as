; definitions and invocations inside a skipped region do nothing at all
%ifdef NEVER
%macro GHOST 1
  mov R1, %1
%endmacro
  GHOST 5
%rep 3
  error_never
%endrep
%else
%macro REAL 1
  mov R1, %1
%endmacro
  REAL 7
%endif
