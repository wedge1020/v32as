; many file-local labels under many functions (v32cc's shape): the output must not depend on whether their names are kept
%define K0 3
%define K1 5
%define K2 9
__function_main:
  call __function_f0
  call __function_f4
  call __function_f8
  call __function_f12
  call __function_f16
  call __function_f20
  call __function_f24
  call __function_f28
  call __function_f32
  call __function_f36
  hlt
__function_f0:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f3
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f4
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L0
  mov R2, __s0
.Lend:
  ret
__function_f1:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f2
  mov R2, __s4
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f2
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L0
  call __function_f2
.Lend:
  ret
__function_f2:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f4
  mov R2, __s0
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f3
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s3
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f6
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L0
.Lend:
  ret
__function_f3:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f4
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f4
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f7
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L0
  call __function_f4
  mov R2, __s4
.Lend:
  ret
__function_f4:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f5
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f5
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f8
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L7
  call __function_f5
  mov R2, __s1
.L7:
  iadd R0, K1
  ilt R1, 7
  jt R1, .L0
.Lend:
  ret
__function_f5:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f6
  mov R2, __s4
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f7
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L0
  mov R2, __s2
.Lend:
  ret
__function_f6:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f10
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f7
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L0
  call __function_f9
.Lend:
  ret
__function_f7:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f9
  mov R2, __s0
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f9
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f8
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L0
.Lend:
  ret
__function_f8:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f9
  mov R2, __s4
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f9
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f10
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L0
  call __function_f12
  mov R2, __s4
.Lend:
  ret
__function_f9:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f13
  mov R2, __s2
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f13
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f13
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L7
  call __function_f12
  mov R2, __s2
.L7:
  iadd R0, K1
  ilt R1, 7
  jt R1, .L0
.Lend:
  ret
__function_f10:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f12
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f12
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L0
  mov R2, __s0
.Lend:
  ret
__function_f11:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f14
  mov R2, __s4
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f15
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L0
  call __function_f15
.Lend:
  ret
__function_f12:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f15
  mov R2, __s4
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f13
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s0
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f16
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L0
.Lend:
  ret
__function_f13:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f15
  mov R2, __s2
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f15
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s3
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f17
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L0
  call __function_f14
  mov R2, __s0
.Lend:
  ret
__function_f14:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f17
  mov R2, __s2
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f17
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f18
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L7
  call __function_f18
  mov R2, __s0
.L7:
  iadd R0, K1
  ilt R1, 7
  jt R1, .L0
.Lend:
  ret
__function_f15:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f16
  mov R2, __s2
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f19
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L0
  mov R2, __s0
.Lend:
  ret
__function_f16:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f17
  mov R2, __s2
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f20
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L0
  call __function_f20
.Lend:
  ret
__function_f17:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f20
  mov R2, __s0
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f21
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f19
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L0
.Lend:
  ret
__function_f18:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f19
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f19
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s1
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f21
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L0
  call __function_f20
  mov R2, __s1
.Lend:
  ret
__function_f19:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f23
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f23
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s0
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f21
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L7
  call __function_f23
  mov R2, __s3
.L7:
  iadd R0, K1
  ilt R1, 7
  jt R1, .L0
.Lend:
  ret
__function_f20:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f23
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f24
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L0
  mov R2, __s4
.Lend:
  ret
__function_f21:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f24
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f24
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s3
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L0
  call __function_f23
.Lend:
  ret
__function_f22:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f24
  mov R2, __s0
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f24
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s1
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f24
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L0
.Lend:
  ret
__function_f23:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f25
  mov R2, __s0
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f27
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f25
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L0
  call __function_f26
  mov R2, __s2
.Lend:
  ret
__function_f24:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f25
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f28
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f27
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L7
  call __function_f27
  mov R2, __s1
.L7:
  iadd R0, K1
  ilt R1, 7
  jt R1, .L0
.Lend:
  ret
__function_f25:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f26
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f29
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L0
  mov R2, __s3
.Lend:
  ret
__function_f26:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f30
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f27
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s3
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L0
  call __function_f30
.Lend:
  ret
__function_f27:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f28
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f28
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s1
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f31
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L0
.Lend:
  ret
__function_f28:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f30
  mov R2, __s0
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f31
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s4
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f29
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L0
  call __function_f29
  mov R2, __s0
.Lend:
  ret
__function_f29:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f31
  mov R2, __s4
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f30
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f30
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L7
  call __function_f30
  mov R2, __s1
.L7:
  iadd R0, K1
  ilt R1, 7
  jt R1, .L0
.Lend:
  ret
__function_f30:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f34
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f33
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L0
  mov R2, __s2
.Lend:
  ret
__function_f31:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f34
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f32
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s0
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L0
  call __function_f35
.Lend:
  ret
__function_f32:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f36
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f36
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f33
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L0
.Lend:
  ret
__function_f33:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f35
  mov R2, __s0
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f36
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f37
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L0
  call __function_f35
  mov R2, __s4
.Lend:
  ret
__function_f34:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f35
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f37
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s1
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f35
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L7
  call __function_f37
  mov R2, __s0
.L7:
  iadd R0, K1
  ilt R1, 7
  jt R1, .L0
.Lend:
  ret
__function_f35:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f38
  mov R2, __s4
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f38
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L0
  mov R2, __s1
.Lend:
  ret
__function_f36:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f39
  mov R2, __s1
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f39
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s1
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L0
  call __function_f38
.Lend:
  ret
__function_f37:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f39
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f39
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s1
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f39
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L0
.Lend:
  ret
__function_f38:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f39
  mov R2, __s0
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f39
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f39
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L0
  call __function_f39
  mov R2, __s1
.Lend:
  ret
__function_f39:
.L0:
  iadd R0, K0
  ilt R1, 0
  jt R1, .L1
  call __function_f39
  mov R2, __s3
.L1:
  iadd R0, K1
  ilt R1, 1
  jt R1, .L2
.L2:
  iadd R0, K2
  ilt R1, 2
  jt R1, .L3
  call __function_f39
.L3:
  iadd R0, K0
  ilt R1, 3
  jt R1, .L4
  mov R2, __s2
.L4:
  iadd R0, K1
  ilt R1, 4
  jt R1, .L5
  call __function_f39
.L5:
  iadd R0, K2
  ilt R1, 5
  jt R1, .L6
.L6:
  iadd R0, K0
  ilt R1, 6
  jt R1, .L7
  call __function_f39
  mov R2, __s0
.L7:
  iadd R0, K1
  ilt R1, 7
  jt R1, .L0
.Lend:
  ret
__s0:
  string "text 0"
__s1:
  string "text 1"
__s2:
  string "text 2"
__s3:
  string "text 3"
__s4:
  string "text 4"
