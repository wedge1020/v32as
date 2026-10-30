;; --- Global Variable RAM Map ---
%define  V32_CART_PAGE            0x20000000
%define  NAN_VALUE                0x7F800000
%define  BOXED_CATEGORY           0x80000000 ; RAM(1), ROM(0)
%define  BOXED_TYPE               0x00400000 ; TABLE/FUNCTION (0), STRING (1)
%define  BOXED_DATA               0xFFC00000 ; common bitmask to indicate boxed data
%define  BOXED_FUNCTION           0x7F800000 ; bitmask for boxed lua function (ROM)
%define  BOXED_ROMSTRING          0x7FC00000 ; bitmank for boxed lua string literal (ROM)
%define  BOXED_TABLE              0xFF800000 ; bitmask for boxed lua table (RAM)
%define  BOXED_RAMSTRING          0xFFC00000 ; starting at offset 4
%define  BOXED_NIL                0xFFC00000
%define  BOXED_FALSE              0xFFC00001
%define  BOXED_BOOLEAN            0xFFC00001
%define  BOXED_TRUE               0xFFC00002
%define  BOXED_TOMBSTONE          0xFFC00003 ; future feature
%define  BOXED_PAYLOAD            0x003FFFFF
%define  TABLE_ARRAYSIZE          0x0000FFFF
%define  BOXED_CLOSURE_FLAG       0x00200000
%define  CLOSURE_ADDR_MASK        0x001FFFFF
%define  HEAP_POINTER             0x00000000
%define  FTOA_SCRATCH_PTR_A       0x00000001
%define  FTOA_SCRATCH_PTR_B       0x00000002
%define  VIRCON32_BTN_PREV_STATE  0x00000003
%define  VIRCON32_SFX_CURSOR      0x0000002F
%define  V32IO_GAMEPAD            0x00000030
%define  VIRCON32_MUSIC_CHANNEL_MASK 0x00000031
%define  VIRCON32_SFX_CHANNEL_MASK   0x00000032
%define  RET_COUNT                0x00000033
%define  MV_BUF                   0x00000034
%define  MV_MAX                   32
%define  META_CALL_DEPTH          0x00000054
%define  META_CALL_STACK          0x00000055
%define  META_CALL_STACK_SIZE     32
%define  VIRCON32_MEMCARD_BASE       0x30000000
%define  VIRCON32_MEMCARD_DATA_BASE  0x30000018
%define  VIRCON32_MEMCARD_CURSOR_ADDR 0x30000017
%define  VIRCON32_MEMCARD_END        0x3003FFFF
%define  var_top                  0x00000075
%define  func_add                 0x00000076
%define  func_main                0x00000077
%define  var_t                    0x00000078
%define  HEAP_START               0x00000079

;; Highest used global RAM address: 0x00000078
;; Dynamic heap will start at runtime address: 0x00000079


    ;@hints 1 abi=caller-save lang=lua
;@src -
;; --- Compiled Code Entry Vector ---
    CALL  __global_scope_initialization  ; Run top-level setups first
    CALL  __function_main ; Execute main execution cycle
    HLT   ; Safe-guard halt

;; --- Function Definitions ---
    JMP   __add_skip
__function_add:
    PUSH  BP
    MOV   BP, SP
    ISUB  SP, 17 ; Reserve stack for locals + spills
    ; --- Default implicit return value(s): nil until overwritten ---
    MOV   R0, BOXED_NIL
    ;@var a loc=BP+2 type=lua
    ;@var b loc=BP+3 type=lua
;@src dbg.lua:5
    MOV   R1, [BP + 2] ; load 'a'
    PUSH  R1 ; spill left operand (protect across possible nested CALL in right operand)
    MOV   R2, [BP + 3] ; load 'b'
    POP   R1 ; reload spilled left operand
    MOV   R0, R1
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __add_num_0
    ;@cold {
    MOV   R0, R1
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R1, R0
    ;@cold }
__add_num_0:
    MOV   R0, R2
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __add_num_1
    ;@cold {
    MOV   R0, R2
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R2, R0
    ;@cold }
__add_num_1:
    FADD  R1, R2
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var sum loc=BP-1 type=lua
    MOV   [BP - 1], R1 ; init local 'sum'
    ;@scope {
    ;@scope }
;@src dbg.lua:8
    MOV   R1, [BP - 1] ; load 'sum'
    MOV   R2, 0x40A00000 ; 5
    MOV   R0, R1
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE
    JT    R0, __add_cmp_slow_3 ; left not a number
    MOV   R0, R1
    FGT   R0, R2
    JF    R0, __add_else_2
    JMP   __add_cmp_done_4
__add_cmp_slow_3:
    PUSH  R1
    PUSH  R2
    CALL  __builtin_relcmp ; -1 / 0 / 1, or 2 if not comparable
    IADD  SP, 2
    IEQ   R0, 1
    JF    R0, __add_else_2
__add_cmp_done_4:
    ;@scope {
;@src dbg.lua:9
    MOV   R1, [BP - 1] ; load 'sum'
    PUSH  R1 ; spill left operand (protect across possible nested CALL in right operand)
    MOV   R2, 0x40000000 ; 2
    POP   R1 ; reload spilled left operand
    FMUL  R1, R2
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var big loc=BP-2 type=lua
    MOV   [BP - 2], R1 ; init local 'big'
;@src dbg.lua:10
    MOV   R1, [BP - 2] ; load 'big'
    PUSH  R1 ; spill left operand (protect across possible nested CALL in right operand)
    MOV   R2, 0x3F800000 ; 1
    POP   R1 ; reload spilled left operand
    FADD  R1, R2
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var huge loc=BP-3 type=lua
    MOV   [BP - 3], R1 ; init local 'huge'
;@src dbg.lua:11
    MOV   R1, [BP - 3] ; load 'huge'
    PUSH  R1 ; spill return value 0 (protect across later nested CALLs)
    POP   R0 ; restore return value 0
    MOV   R1, 1
    MOV   [RET_COUNT], R1 ; number of values returned
    JMP   __add_return
    ;@scope }
    JMP   __add_end_if_2
__add_else_2:
__add_end_if_2:
;@src dbg.lua:13
    MOV   R1, [BP - 1] ; load 'sum'
    PUSH  R1 ; spill return value 0 (protect across later nested CALLs)
    POP   R0 ; restore return value 0
    MOV   R1, 1
    MOV   [RET_COUNT], R1 ; number of values returned
    JMP   __add_return
    ; --- Re-establish Nil for IMPLICIT (fall-through) return ---
    MOV   R0, BOXED_NIL ; implicit return: fell off the end without 'return'
    MOV   R1, 0
    MOV   [RET_COUNT], R1 ; no values returned
__add_return:
    MOV   SP, BP
    POP   BP
    RET  

    ;@fn __function_add escapes=none
__add_skip:
;@src dbg.lua:13
    JMP   __main_skip
__function_main:
;@src dbg.lua:13
    PUSH  BP
    MOV   BP, SP
    ISUB  SP, 16 ; Reserve stack for locals + spills
    ; --- Default implicit return value(s): nil until overwritten ---
    MOV   R0, BOXED_NIL
    ;; Load and box address of the mangled function
;@src dbg.lua:32
    MOV   R2, __function_add
    OR    R2, BOXED_FUNCTION ; Box as Function
    MOV   [BP -6], R2 ; REGSPILL: Store to spill slot
    ; --- Pushing explicit arguments Right-to-Left ---
    MOV   R3, 0x40000000 ; 2
    PUSH  R3
    MOV   R3, 0x3F800000 ; 1
    PUSH  R3
    MOV   R2, [BP -6] ; REGSPILL: Load from spill slot
    MOV   R0, R2 ; Prepare boxed target for validation
    CALL  __builtin_exec ; Validate and execute
    IADD  SP, 2 ; Clean up call arguments
    MOV   R1, R0 ; Store return value
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var t loc=BP-1 type=lua
    MOV   [BP - 1], R1 ; init local 't'
__main_while_start_0:
;@src dbg.lua:33
    MOV   R1, [BP - 1] ; load 't'
    MOV   R2, 0x40A00000 ; 5
    MOV   R0, R1
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE
    JT    R0, __main_cmp_slow_1 ; left not a number
    MOV   R0, R1
    FLT   R0, R2
    JF    R0, __main_while_end_0
    JMP   __main_cmp_done_2
__main_cmp_slow_1:
    PUSH  R1
    PUSH  R2
    CALL  __builtin_relcmp ; -1 / 0 / 1, or 2 if not comparable
    IADD  SP, 2
    IEQ   R0, -1
    JF    R0, __main_while_end_0
__main_cmp_done_2:
    ;@scope {
;@src dbg.lua:34
    MOV   R1, [BP - 1] ; load 't'
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var w loc=BP-2 type=lua
    MOV   [BP - 2], R1 ; init local 'w'
;@src dbg.lua:35
    MOV   R1, [BP - 1] ; load 't'
    PUSH  R1 ; spill left operand (protect across possible nested CALL in right operand)
    MOV   R2, 0x3F800000 ; 1
    POP   R1 ; reload spilled left operand
    MOV   R0, R1
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __main_num_3
    ;@cold {
    MOV   R0, R1
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R1, R0
    ;@cold }
__main_num_3:
    FADD  R1, R2
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    MOV   [BP - 1], R1 ; store 't'
    ;@scope }
    JMP   __main_while_start_0
__main_while_end_0:
    ; --- Re-establish Nil for IMPLICIT (fall-through) return ---
    MOV   R0, BOXED_NIL ; implicit return: fell off the end without 'return'
    MOV   R1, 0
    MOV   [RET_COUNT], R1 ; no values returned
__main_return:
    MOV   SP, BP
    POP   BP
    RET  

    ;@fn __function_main escapes=none
__main_skip:
;@src -

; --- Global Variable & Runtime Initialization ---
__global_scope_initialization:
;@src dbg.lua:35
    PUSH  BP
    MOV   BP, SP
    ISUB  SP, 23 ; Reserve stack for top-level locals + spills
    MOV   R0, HEAP_START ; heap start (see variable map)
    MOV   [HEAP_POINTER], R0
    CALL  __globals_to_nil
    MOV   R0, 0
    MOV   [FTOA_SCRATCH_PTR_A], R0 ; must start at 0 -- see generate_global_setup() comment
    MOV   [FTOA_SCRATCH_PTR_B], R0 ; must start at 0 -- see generate_global_setup() comment
    MOV   R1, VIRCON32_BTN_PREV_STATE ; zero-init btnp() prev-state     table
    MOV   R2, 44
    MOV   R3, 0
__vircon32_btn_prev_state_zero_loop:
    JF    R2, __vircon32_btn_prev_state_zero_done
    MOV   [R1], R3
    IADD  R1, 1
    ISUB  R2, 1
    JMP   __vircon32_btn_prev_state_zero_loop
__vircon32_btn_prev_state_zero_done:
    OUT   INP_SelectedGamepad, 0 ; gamepad 0 selected, and known to be
    MOV   R1, 0
    MOV   [V32IO_GAMEPAD], R1 ; (the port can't be read back: v32io.s)
    MOV   R1, 1 ; sfx.play() round-robin starts at channel 1
    MOV   [VIRCON32_SFX_CURSOR], R1
    MOV   R1, 0 ; no channels claimed by either namespace yet
    MOV   [VIRCON32_MUSIC_CHANNEL_MASK], R1
    MOV   [VIRCON32_SFX_CHANNEL_MASK], R1
    MOV   [META_CALL_DEPTH], R1 ; no __call trampolines pending
    MOV   R0, BOXED_NIL
    MOV   [var_top], R0 ; nil-init global 'top'
    MOV   [var_t], R0 ; nil-init global 't'
;@src dbg.lua:2
    MOV   R2, 0x41200000 ; 10
    PUSH  R2 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R2 ; Pass 2: retrieve evaluated RHS value for target #0
    MOV   [var_top], R2 ; init local 'top'
;@src dbg.lua:16
    MOV   R2, 0x40400000 ; 3
    MOV   [BP - 1], R2 ; Save loop limit to stack
    MOV   R2, 1.000000 ; Default step = 1.0
    MOV   [BP - 2], R2 ; Save loop step to stack
    MOV   R2, 0x3F800000 ; 1
    MOV   [BP - 3], R2 ; Save loop control counter to stack
    MOV   [BP - 4], R2 ; init local 'i'
    ;@loop kind=for counter=-3 limit=-1 step=-2 stepval=1 from=1 to=3
__global_for_start_0:
    MOV   R2, [BP - 3]
    MOV   R4, R2 ; preserve control value -- FGT/FLT below is destructive
    MOV   R3, [BP - 1]
    FGT   R2, R3 ; Check if control > limit (exit condition)
    JT    R2, __global_for_end_0 ; Jump to end if loop condition fails
    MOV   [BP - 4], R4 ; init local 'i'
;@src dbg.lua:17
    MOV   R2, [BP - 4] ; load 'i'
    PUSH  R2 ; spill left operand (protect across possible nested CALL in right operand)
    MOV   R3, [BP - 4] ; load 'i'
    POP   R2 ; reload spilled left operand
    MOV   R0, R2
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __global_num_1
    ;@cold {
    MOV   R0, R2
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R2, R0
    ;@cold }
__global_num_1:
    MOV   R0, R3
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __global_num_2
    ;@cold {
    MOV   R0, R3
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R3, R0
    ;@cold }
__global_num_2:
    FMUL  R2, R3
    PUSH  R2 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R2 ; Pass 2: retrieve evaluated RHS value for target #0
    MOV   [BP - 5], R2 ; init local 'sq'
    ;; --- Intrinsic: print(x, y, value) ---
;@src dbg.lua:18
    MOV   R3, 0x00000000 ; 0
    CFI   R3 ; Convert X to hardware integer
    PUSH  R3 ; Save X coordinate
    MOV   R3, 0x00000000 ; 0
    CFI   R3 ; Convert Y to hardware integer
    PUSH  R3 ; Save Y coordinate
    MOV   R3, [BP - 5] ; load 'sq'
    PUSH  R3 ; Push raw value to convert
    CALL  __builtin_tostring
    MOV   [SP], R0 ; overwrite raw value with the string pointer
    CALL  __builtin_print
    IADD  SP, 3 ; Clean up x, y, and string from the stack
    MOV   R2, [BP - 3]
    MOV   R3, [BP - 2]
;@src dbg.lua:16
    FADD  R2, R3 ; control += step
    MOV   [BP - 3], R2 ; Save incremented control counter
    JMP   __global_for_start_0 ; Loop back to condition
__global_for_end_0:
;@src dbg.lua:22
    MOV   R2, 0x3F800000 ; 1
    PUSH  R2 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    MOV   R2, 0x40000000 ; 2
    PUSH  R2 ; Pass 1: hold evaluated RHS value #1 until all targets are known
    POP   R2 ; Pass 2: retrieve evaluated RHS value for target #1
    MOV   [BP - 1], R2 ; init local 'y'
    POP   R2 ; Pass 2: retrieve evaluated RHS value for target #0
    MOV   [BP - 2], R2 ; init local 'x'
;@src dbg.lua:24
    MOV   R2, [BP - 2] ; load 'x'
    PUSH  R2 ; spill left operand (protect across possible nested CALL in right operand)
    MOV   R3, [BP - 1] ; load 'y'
    POP   R2 ; reload spilled left operand
    MOV   R0, R2
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __global_num_3
    ;@cold {
    MOV   R0, R2
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R2, R0
    ;@cold }
__global_num_3:
    MOV   R0, R3
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __global_num_4
    ;@cold {
    MOV   R0, R3
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R3, R0
    ;@cold }
__global_num_4:
    FADD  R2, R3
    PUSH  R2 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R2 ; Pass 2: retrieve evaluated RHS value for target #0
    MOV   [BP - 3], R2 ; init local 'z'
    ;; --- Intrinsic: print(x, y, value) ---
;@src dbg.lua:25
    MOV   R3, 0x00000000 ; 0
    CFI   R3 ; Convert X to hardware integer
    PUSH  R3 ; Save X coordinate
    MOV   R3, 0x00000000 ; 0
    CFI   R3 ; Convert Y to hardware integer
    PUSH  R3 ; Save Y coordinate
    MOV   R3, [BP - 3] ; load 'z'
    PUSH  R3 ; Push raw value to convert
    CALL  __builtin_tostring
    MOV   [SP], R0 ; overwrite raw value with the string pointer
    CALL  __builtin_print
    IADD  SP, 3 ; Clean up x, y, and string from the stack
    ;; --- Intrinsic: print(x, y, value) ---
;@src dbg.lua:29
    MOV   R2, 0x00000000 ; 0
    CFI   R2 ; Convert X to hardware integer
    PUSH  R2 ; Save X coordinate
    MOV   R2, 0x00000000 ; 0
    CFI   R2 ; Convert Y to hardware integer
    PUSH  R2 ; Save Y coordinate
    ;; Load and box address of the mangled function
    MOV   R3, __function_add
    OR    R3, BOXED_FUNCTION ; Box as Function
    MOV   [BP -14], R3 ; REGSPILL: Store to spill slot
    ; --- Pushing explicit arguments Right-to-Left ---
    MOV   R4, 0x40000000 ; 2
    PUSH  R4
    MOV   R4, [var_top] ; load 'top'
    PUSH  R4
    MOV   R3, [BP -14] ; REGSPILL: Load from spill slot
    MOV   R0, R3 ; Prepare boxed target for validation
    CALL  __builtin_exec ; Validate and execute
    IADD  SP, 2 ; Clean up call arguments
    MOV   R2, R0 ; Store return value
    PUSH  R2 ; Push raw value to convert
    CALL  __builtin_tostring
    MOV   [SP], R0 ; overwrite raw value with the string pointer
    CALL  __builtin_print
    IADD  SP, 3 ; Clean up x, y, and string from the stack
    ;; Load and box address of the mangled function
;@src dbg.lua:37
    MOV   R2, __function_main
    OR    R2, BOXED_FUNCTION ; Box as Function
    PUSH  R2 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R2 ; Pass 2: retrieve evaluated RHS value for target #0
    MOV   [func_main], R2 ; store 'main'
    MOV   SP, BP
    POP   BP
    RET  
;@src -
;; runtime stubs
__arith_coerce:
    RET
__builtin_exec:
    RET
__builtin_print:
    RET
__builtin_relcmp:
    RET
__builtin_tostring:
    RET
__globals_to_nil:
    RET
