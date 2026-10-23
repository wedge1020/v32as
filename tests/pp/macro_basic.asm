; named parameters, defaults, positional references and the argument count
%macro CLAMP reg, lo=0, hi=255
    imax  reg, lo
    imin  reg, hi
%endmacro

%macro SETPAIR 2
    mov   %1, %2
    mov   %2, %1
%endmacro

%macro PUT 1
    integer %1
%endmacro

%macro COUNT 0
    mov   R9, %0
%endmacro

%macro NARGS 1-3
    mov   R8, %0
%endmacro

_start:
    CLAMP R1
    CLAMP R2, 5
    CLAMP R3, 5, 9
    CLAMP R4, , 7              ; an empty argument takes the default
    SETPAIR R5, R6
    COUNT
    NARGS 1
    NARGS 1, 2
    NARGS 1, 2, 3
_here: CLAMP R7, 1, 2          ; labels in front of the invocation
    PUT {1, 2, 3}              ; braces keep the commas inside one argument
    PUT -4
    hlt
