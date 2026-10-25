; program start section
  call __v32mod_noinit
  call __function_main
  hlt
; the data base: addresses of globals are __data_base+N, data relocations for v32as -r
  datasym __data_base
__function_bya_read_s0:
  push BP
  mov BP, SP
  isub SP, 11
.for_1_start:
  mov R0, [BP+5]
  igt R0, 0
  jf R0, .for_1_end
  mov R0, [BP+4]
  and R0, 3
  mov [BP-1], R0
  mov R0, [BP+5]
  mov R1, [BP-2]
  isub R0, R1
  mov [BP+5], R0
.for_1_continue:
  jmp .for_1_start
.for_1_end:
.return:
  mov SP, BP
  pop BP
  ret
__function_bya_read_dir_s0:
  mov [BP-20], R0
.for_5_start:
  mov R0, [BP-20]
  ilt R0, 192
  jf R0, .for_5_end
  mov R0, [__data_base+1]
  mov R0, [R0+12]
  mov R1, [BP+2]
  mov [SP], R1
  lea R1, [BP-18]
  mov R0, 0
  mov [BP-21], R0
.for_6_start:
  mov R0, [BP-21]
  ilt R0, 16
  jf R0, .for_6_end
  lea R0, [BP-18]
  mov R1, [BP-21]
  iadd R0, R1
  mov R0, [R0]
  mov [BP-23], R0
  mov R0, 0
  mov [BP-22], R0
.for_7_start:
  mov R0, [BP-22]
  ilt R0, 4
  jf R0, .for_7_end
  mov R0, [BP-21]
  iadd R1, 1
  mov [BP-22], R1
  jmp .for_7_start
.for_7_end:
.for_6_continue:
  mov R0, [BP-21]
  mov R1, R0
  iadd R1, 1
  mov [BP-21], R1
  jmp .for_6_start
.for_6_end:
  mov R0, 0
  mov R1, [BP-19]
  mov [R1+62], R0
  mov R0, [BP-2]
  mov R1, [BP-19]
  mov [R1+63], R0
  mov R0, [BP-1]
  mov R1, [BP-19]
  mov [R1+64], R0
.for_5_continue:
  mov R0, [BP-20]
  mov R1, R0
  iadd R1, 1
  mov [BP-20], R1
  jmp .for_5_start
.for_5_end:
.for_11_start:
  mov R0, [BP+5]
  igt R0, 0
  jf R0, .for_11_end
  mov R0, [BP+4]
  and R0, 3
  mov [BP-1], R0
  mov R0, 512
  mov R1, [BP-1]
  mov R1, [BP-2]
  isub R0, R1
  mov [BP+5], R0
.for_11_continue:
  jmp .for_11_start
.for_11_end:
.return:
  mov SP, BP
  pop BP
  ret
__function_bya_write_dir_s0:
  push BP
  mov BP, SP
  isub SP, 29
  mov R0, 0
  mov [BP-20], R0
.for_18_start:
  imul R1, 65
  iadd R0, R1
  mov [BP-19], R0
  mov R0, 0
  mov [BP-25], R0
  mov R0, 0
  mov [BP-21], R0
.for_19_start:
  mov R0, [BP-21]
  mov [R1], R0
.for_19_continue:
  mov R0, [BP-21]
  mov R1, R0
  iadd R1, 1
  mov [BP-21], R1
  jmp .for_19_start
.for_19_end:
  mov R0, [BP-19]
  mov [BP-17], R1
.if_26_end:
.for_27_start:
  mov R0, [BP+2]
  mov R1, [BP-17]
  iadd R0, R1
  mov R0, [R0]
  ine R0, 0
  jf R0, .for_27_end
  mov [BP-18], R1
.if_28_end:
  mov R0, [BP-17]
  mov R1, R0
  iadd R1, 1
  mov [BP-17], R1
.for_27_continue:
  jmp .for_27_start
.for_27_end:
  mov R0, 1
.return:
  mov SP, BP
  pop BP
  ret

__function_bya_show_s0:
  push BP
  mov BP, SP
  call __function_bya_show_s0
  mov R0, 0
  mov [BP-15], R0
.for_44_start:
  mov R0, [BP-15]
  ilt R0, 18
  jf R0, .for_44_end
  mov R0, [BP-15]
  ieq R0, 2
  mov R1, R0
  iadd R1, 1
  mov [BP-15], R1
  jmp .for_44_start
.for_44_end:
.return:
  mov SP, BP
  pop BP
  ret

__function_main:
  call R0
.return:
  mov SP, BP
  pop BP
  ret

; read-only data
  string "BYTEADDR"
__literal_string_12:
  string "byte"

__v32mod_noinit:
