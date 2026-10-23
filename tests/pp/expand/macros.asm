%macro CLAMP reg, lo=0, hi=255
    imax  reg, lo           ; keep this comment
    imin  reg, hi
%endmacro
%macro LOOP n
%rep n
    %%x_%@: nop
%endrep
%endmacro

_start:
    CLAMP R1
before: CLAMP R2, 5, 9
    LOOP 2
    mov R3, 1
