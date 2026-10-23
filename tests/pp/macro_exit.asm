; %exitmacro leaves a macro even from inside a %if or a %rep, %exitrep only the loop
%macro CLAMPHI reg, limit
    %if limit == 0
        mov   reg, 0
        %exitmacro
    %endif
    %rep 3
        imin  reg, limit
        %exitrep
    %endrep
    iadd  reg, 1
%endmacro

%macro SCAN n
    %assign i 0
    %rep n
        %if i == 2
            %exitmacro             ; leaves the macro, not just the loop
        %endif
        iadd  R1, i
        %assign i i + 1
    %endrep
    mov   R2, 99
%endmacro

    CLAMPHI R1, 0
    CLAMPHI R2, 7
    SCAN 5
    SCAN 1
    hlt
