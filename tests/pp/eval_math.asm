%define WIDTH 320
%define MATH WIDTH+1
  mov R0, %eval(WIDTH * 2 + 1)
  mov R1, %eval(MATH * 2)              ; textual, like C: 320 + 1*2
  mov R2, %eval(0x10 | 0b11)
  mov R3, %eval(-7 / 2)
  mov R4, %eval(-7 % 3)
  mov R5, %eval(1 << 31)
  mov R6, %eval(0x7FFFFFFF + 1)        ; wraps like the CPU
  mov R7, %eval('A' + 1)
  mov R8, %eval(WIDTH > 300 ? 10 : 1/0)   ; the untaken branch is not evaluated
  mov R9, %eval(!0 + ~0)
  mov R10, %eval((3 > 2) + (2 >= 2) + (1 == 1) + (1 != 1))
  mov R11, %eval(min(3, 4) + max(3, 4) + abs(-5))
  mov R12, %eval(2Ah + #3 + 052)
  float %eval(1.5 * 2)
  float %eval(WIDTH / 3.0)
  float %eval(1 / 2.0)
  float %eval(float(7) / 2)
  mov R13, %eval(int(2.9) + int(-2.9))
  float %eval(-0.25)
  float %eval(floor(2.5) + ceil(2.5) + round(2.5))
  float %eval(0.1 + 0.2)
  mov R14, %eval((WIDTH == 320) && (1 || 1/0))
