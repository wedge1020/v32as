; operand tests pick the right form
%macro INC 1
  %ifreg %1
    iadd %1, 1
  %elifnum %1
    %error "INC wants a register"
  %else
    %error "INC: not an operand"
  %endif
%endmacro

%macro LOADANY 2
  %ifreg %2
    mov %1, %2
  %elifnum %2
    mov %1, %2
  %elifstr %2
    mov %1, _text
  %elifport %2
    in  %1, %2
  %elifidni %2, ZERO
    mov %1, 0
  %else
    mov %1, %2              ; a label: its address
  %endif
%endmacro

%define SRC R7
%define TEN 10

_start:
    INC R1
    INC SRC
    LOADANY R1, R2
    LOADANY R1, 5
    LOADANY R1, %eval(TEN + 1)
    LOADANY R1, "hi"
    LOADANY R1, GPU_RemainingPixels
    LOADANY R1, zero
    LOADANY R1, _start
    %ifnreg TEN
      mov R3, 1
    %endif
    %ifnnum SRC
      mov R3, 2
    %endif
    %ifidn TEN, 10
      mov R3, 3                ; the operands are expanded first
    %endif
    %ifempty
      mov R3, 4
    %endif
    %ifnempty TEN
      mov R3, 5
    %endif
    %ifmacro INC
      mov R3, 6
    %endif
    %ifnmacro DEC
      mov R3, 7
    %endif
    %iflabel _start
      mov R3, 8
    %endif
    %ifsysreg IP
      mov R3, 9
    %endif
    hlt
_text:
    string "hi"
