; macros are shared by everything that is preprocessed together: an include defines
; them, later code uses them, and %includeonce keeps them from being defined twice
%include "incs/macros.inc"
%include "incs/macros.inc"
    BUMP R1
    BUMP R2, 5
    hlt
