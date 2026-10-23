; %strlen %substr %strcat %defstr
%strlen LEN "hello"
%substr MID "hello", 2, 3
%substr TAIL "hello", 4
%strcat BOTH "ab", "cd", "\n"
%define WORD "xyz"
%strlen WL WORD
%defstr STR 1 + 2

    mov   R1, LEN
    mov   R2, WL
    string MID
    string TAIL
    string BOTH
    string STR
    string "end"
