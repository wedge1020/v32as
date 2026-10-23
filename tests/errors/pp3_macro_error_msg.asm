%macro NEEDREG 1
 %ifreg %1
 %else
  %error "NEEDREG wants a register, got %1"
 %endif
%endmacro
NEEDREG 42
