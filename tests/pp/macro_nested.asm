; macros calling macros, arguments forwarded, a macro made by a define
%define LIMIT 100

%macro LOAD 2
    mov   %1, %2
%endmacro

%macro LOAD2 4
    LOAD  %1, %2
    LOAD  %3, %4
%endmacro

%macro SETUP r
    LOAD2 r, LIMIT, R2, %eval(LIMIT + 1)
%endmacro

    SETUP R1
    LOAD2 R3, 1, R4, 2
    hlt
