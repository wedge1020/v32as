; %rep: counts, nesting, early exit, %%labels, counters
%assign n 0
%rep 3
    mov   R1, n
%assign n n + 1
%endrep

%rep 2
  %rep 2
    wait
  %endrep
    hlt
%endrep

%rep 0
    error_never_assembled
%endrep

%assign k 0
%rep 100
  %if k == 3
    %exitrep
  %endif
    iadd  R2, k
%assign k k + 1
%endrep

%rep 2
%%spin:                      ; a fresh label on every pass
    jt    R3, %%spin
%endrep

%macro WAITN n=1
  %rep n
    wait
  %endrep
%endmacro
    WAITN
    WAITN 3
    hlt
