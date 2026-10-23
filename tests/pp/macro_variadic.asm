; variadic macros, %rotate, %0 and %*
%macro PUSHALL regs...
  %rep %0
    push  %1
    %rotate 1
  %endrep
%endmacro

%macro POPALL regs...
  %rep %0
    %rotate -1
    pop   %1
  %endrep
%endmacro

%macro DATA first, rest...
    integer first
  %if %0 > 1
    integer rest
  %endif
    integer %*
%endmacro

%macro ANY ...
    integer %0
%endmacro

    PUSHALL R1, R2, R3
    POPALL R1, R2, R3
    DATA 1, 2, 3, 4
    DATA 9
    ANY
    ANY 5, 6
    hlt
