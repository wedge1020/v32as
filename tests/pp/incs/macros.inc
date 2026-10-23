%includeonce
%macro BUMP 1-2
  %if %0 == 2
    iadd %1, %2
  %else
    iadd %1, 1
  %endif
%endmacro
