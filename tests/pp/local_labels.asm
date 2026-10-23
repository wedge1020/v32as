_first:
.loop:
  isub R1, 1
  jt R1, .loop
  mov R2, 3.5            ; a float is not a local label
  mov R3, '.'            ; neither is a character
  string ".loop"         ; nor text in a string
_second:
.loop:
  jmp .loop              ; this is _second__loop
  jmp _first__loop
