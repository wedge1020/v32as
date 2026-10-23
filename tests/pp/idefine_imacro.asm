; case-insensitive macros and defines
%idefine Limit 10
%imacro Bump 1
    iadd %1, Limit
%endmacro

    bump R1
    BUMP R2
    bUmP R3
    mov R4, LIMIT
    mov R5, limit
%undef LIMIT
    mov R6, 5
%define Fixed 3
    mov R7, Fixed
%xdefine TWICE Fixed + Fixed
%define Fixed 100
    mov R8, %eval(TWICE)
