; %push / %pop contexts and the structured-control library built on them
%include "../../lib/control.inc"

_main:
    IF R1
        mov   R2, 1
        IF R3                    ; nested
            mov   R2, 2
        ELSE
            mov   R2, 3
        ENDIF
    ELSE
        mov   R2, 4
    ENDIF
    WHILE R4
        isub  R4, 1
        IF R5
            iadd R6, 1
        ENDIF
    ENDWHILE
    hlt
