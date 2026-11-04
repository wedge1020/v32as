; v32as.inc: small helper macros for v32as (needs the extended preprocessor, so not --standard)
;
;   %include "v32as.inc"          (put this directory on the path with -I lib)
;
; Every macro checks its operands with the %if tests, so a mistake is reported at the
; line that made it:   INC 5   ->   error: INC: the first operand must be a register
; Macros that need a scratch register or the stack say so.
;
;   INC reg [, n]        reg += n        (n defaults to 1)
;   DEC reg [, n]        reg -= n
;   ZERO reg ...         set each register to 0
;   NEG reg              reg = -reg
;   CLAMP reg, lo, hi    keep reg between lo and hi (integers)
;   SWAP a, b            exchange two registers (uses the stack)
;   PUSHALL r, ...       push each register, in order
;   POPALL r, ...        pop them again in reverse, so POPALL takes the same list
;   WAIT_FRAMES [n]      wait n frames (default 1)
;
%includeonce

%macro INC 1-2
  %ifnreg %1
    %error "INC: the first operand must be a register"
  %elifempty %2
    iadd  %1, 1
  %else
    iadd  %1, %2
  %endif
%endmacro

%macro DEC 1-2
  %ifnreg %1
    %error "DEC: the first operand must be a register"
  %elifempty %2
    isub  %1, 1
  %else
    isub  %1, %2
  %endif
%endmacro

%macro ZERO 1-*
  %rep %0
    %ifnreg %1
      %error "ZERO: every operand must be a register"
    %else
      mov   %1, 0
    %endif
    %rotate 1
  %endrep
%endmacro

%macro NEG 1
  %ifnreg %1
    %error "NEG: the operand must be a register"
  %else
    imul  %1, -1
  %endif
%endmacro

%macro CLAMP 3
  %ifnreg %1
    %error "CLAMP: the first operand must be a register"
  %else
    imax  %1, %2
    imin  %1, %3
  %endif
%endmacro

%macro SWAP 2
  %ifnreg %1
    %error "SWAP: both operands must be registers"
  %elifnreg %2
    %error "SWAP: both operands must be registers"
  %elifidn %1, %2
    ; swapping a register with itself does nothing
  %else
    push  %1
    mov   %1, %2
    pop   %2
  %endif
%endmacro

%macro PUSHALL 1-*
  %rep %0
    %ifnreg %1
      %error "PUSHALL: every operand must be a register"
    %else
      push  %1
    %endif
    %rotate 1
  %endrep
%endmacro

%macro POPALL 1-*
  %rep %0
    %rotate -1
    %ifnreg %1
      %error "POPALL: every operand must be a register"
    %else
      pop   %1
    %endif
  %endrep
%endmacro

%macro WAIT_FRAMES n=1
  %rep n
    wait
  %endrep
%endmacro
