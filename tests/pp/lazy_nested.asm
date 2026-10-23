%define A B            ; B is not defined yet: bodies expand where they are USED
%define B 7
%define LIMIT A
  mov R0, LIMIT
%define B 9            ; redefinition is silent and takes effect for later uses
  mov R1, LIMIT
%undef B
%ifdef B
  %error "B should be gone"
%else
  mov R2, 1
%endif
%ifndef NEVER
  %ifdef NEVER
    mov R3, 111
    %ifdef ALSO_NEVER
      mov R3, 222
    %else
      mov R3, 333
    %endif
  %else
    mov R3, 4
  %endif
%endif
