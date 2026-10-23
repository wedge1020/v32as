; control.inc: structured IF / ELSE / ENDIF and WHILE / ENDWHILE on top of jf / jmp
;
;   %include "control.inc"
;
;       IF R1                  ; run the block when R1 is not zero
;           ...
;       ELSE                   ; (optional)
;           ...
;       ENDIF
;
;       WHILE R2               ; repeat while R2 is not zero
;           ...
;       ENDWHILE
;
; They nest, because each opens a %push context whose private labels (%$else, %$end,
; %$top) are different every time. A mismatched ELSE / ENDIF / ENDWHILE is reported.
;
%includeonce

%macro IF 1
  %ifnreg %1
    %error "IF: the condition must be a register"
  %endif
  %push if
    jf    %1, %$else
%endmacro

%macro ELSE 0
  %ifctx if
    jmp   %$end
%$else:
  %repl else
  %else
    %error "ELSE without a matching IF"
  %endif
%endmacro

%macro ENDIF 0
  %ifctx if
%$else:
    %pop
  %elifctx else
%$end:
    %pop
  %else
    %error "ENDIF without a matching IF"
  %endif
%endmacro

%macro WHILE 1
  %ifnreg %1
    %error "WHILE: the condition must be a register"
  %endif
  %push while
%$top:
    jf    %1, %$end
%endmacro

%macro ENDWHILE 0
  %ifctx while
    jmp   %$top
%$end:
    %pop
  %else
    %error "ENDWHILE without a matching WHILE"
  %endif
%endmacro
