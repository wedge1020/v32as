; a macro may take the name of an instruction (the pragma silences the warning)
%pragma shadow PUSH, pop
%macro PUSH 1-*
  %rep %0
    push %1
    %rotate 1
  %endrep
%endmacro
%imacro pop 1
    iadd %1, 1
%endmacro
    PUSH R1, R2
    POP R3
