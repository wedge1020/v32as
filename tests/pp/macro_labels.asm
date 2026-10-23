; %%labels are unique per expansion; .labels still follow the last global label
%macro COUNTDOWN reg, count
    mov   reg, count
%%top:
    isub  reg, 1
    jt    reg, %%top
%endmacro

%macro SKIP_IF_ZERO reg
    jf    reg, %%done
    iadd  reg, 1
%%done:
%endmacro

_main:
    COUNTDOWN R1, 3
    COUNTDOWN R2, 4
    SKIP_IF_ZERO R3
    SKIP_IF_ZERO R4
.again:
    isub  R5, 1
    jt    R5, .again
    hlt
