%include "inc2.asm"
;@src main.lua:5
__function_main:
    mov R0, 1
    call __function_helper
    hlt
