; the shipped macro library
%include "../../lib/v32as.inc"
%include "../../lib/v32as.inc"          ; %includeonce

    INC R1
    INC R2, 5
    DEC R3
    DEC R4, 2
    ZERO R5, R6, R7
    NEG R8
    CLAMP R9, 0, 100
    SWAP R1, R2
    SWAP R3, R3
    PUSHALL R1, R2, R3
    POPALL R1, R2, R3
    WAIT_FRAMES
    WAIT_FRAMES 2
    hlt
