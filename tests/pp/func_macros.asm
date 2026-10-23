%define HALF(x) ((x) / 2)
%define ADD3(a, b, c) a + b + c
%define LAB(n) _label_ ## n
%define JOIN(a, b) a ## b
%define STR(x) #x
%define LOG(fmt, ...) fmt , ## __VA_ARGS__
%define OUTER(x) INNER(x) + 1
%define INNER(y) (y * 2)
%define PUSHER PUSH2            ; an object-like macro naming a function-like one
%define PUSH2(a, b) push a
%define NOARGS() 7
%define SWAP2(a, b) b, a
%define W 4
LAB(1):
  mov R0, %eval(HALF(W * 10))
  mov R1, %eval(ADD3(1, 2, 3) * 2)
  mov R2, %eval(OUTER(3))
  PUSHER(R3, R4)
  mov R5, NOARGS()
  integer LOG(1)
  integer LOG(1, 2, 3)
  integer SWAP2(5, 6)
  string STR(  a   "q"   'c' )
JOIN(_lab, el_2):
  jmp LAB(1)
  jmp JOIN(_label_, 2)
  mov R6, %eval(HALF(HALF(W)))
