%macro TWICE 1
    iadd  %1, 1
    iadd  %1, 1
%endmacro
_go:
    TWICE R1
    mov   R2, 3
    TWICE R2
