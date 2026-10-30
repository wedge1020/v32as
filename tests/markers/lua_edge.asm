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
%define  func_f                   0x00000075
%define  var_g                    0x00000076
%define  func_main                0x00000077
%define  var_r                    0x00000078
%define  func_g__l0               0x00000079
%define  HEAP_START               0x0000007A

;; Highest used global RAM address: 0x00000079
;; Dynamic heap will start at runtime address: 0x0000007A


    ;@hints 1 abi=caller-save lang=lua
;@src -
;; --- Compiled Code Entry Vector ---
    CALL  __global_scope_initialization  ; Run top-level setups first
    CALL  __function_main ; Execute main execution cycle
    HLT   ; Safe-guard halt

;; --- Function Definitions ---
    JMP   __f_skip
__function_f:
    PUSH  BP
    MOV   BP, SP
    ISUB  SP, 25 ; Reserve stack for locals + spills
    MOV   [BP -25], R13 ; save caller's argument count (variadic ABI)
    ; --- Default implicit return value(s): nil until overwritten ---
    MOV   R0, BOXED_NIL
    ;@var n loc=BP+2 type=lua
;@src edge.lua:2
    MOV   R1, [BP + 2] ; load 'n'
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var a loc=BP-1 type=lua
    MOV   [BP - 1], R1 ; init local 'a'
;@src edge.lua:3
    MOV   R1, [BP - 1] ; load 'a'
    PUSH  R1 ; spill left operand (protect across possible nested CALL in right operand)
    MOV   R2, 0x3F800000 ; 1
    POP   R1 ; reload spilled left operand
    MOV   R0, R1
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __f_num_0
    ;@cold {
    MOV   R0, R1
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R1, R0
    ;@cold }
__f_num_0:
    FADD  R1, R2
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    MOV   [BP - 1], R1 ; init local 'a'
    ;@scope {
    ;@scope {
;@src edge.lua:6
    MOV   R1, 0x42C60000 ; 99
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var a loc=BP-2 type=lua
    MOV   [BP - 2], R1 ; init local 'a'
    ;@scope }
    ;@scope }
    ;@scope {
;@src edge.lua:9
    MOV   R1, 0x40000000 ; 2
    MOV   [BP - 2], R1 ; Save loop limit to stack
    MOV   R1, 1.000000 ; Default step = 1.0
    MOV   [BP - 3], R1 ; Save loop step to stack
    MOV   R1, 0x3F800000 ; 1
    MOV   [BP - 4], R1 ; Save loop control counter to stack
    ;@var i loc=BP-5 type=lua
    MOV   [BP - 5], R1 ; init local 'i'
    ;@loop kind=for counter=-4 limit=-2 step=-3 stepval=1 from=1 to=2
__f_for_start_1:
    MOV   R1, [BP - 4]
    MOV   R3, R1 ; preserve control value -- FGT/FLT below is destructive
    MOV   R2, [BP - 2]
    FGT   R1, R2 ; Check if control > limit (exit condition)
    JT    R1, __f_for_end_1 ; Jump to end if loop condition fails
    MOV   [BP - 5], R3 ; init local 'i'
;@src edge.lua:10
    MOV   R1, [BP - 5] ; load 'i'
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var k loc=BP-6 type=lua
    MOV   [BP - 6], R1 ; init local 'k'
    MOV   R1, [BP - 4]
    MOV   R2, [BP - 3]
;@src edge.lua:9
    FADD  R1, R2 ; control += step
    MOV   [BP - 4], R1 ; Save incremented control counter
    JMP   __f_for_start_1 ; Loop back to condition
__f_for_end_1:
    ;@scope }
;@src edge.lua:12
    MOV   R1, 0x40A00000 ; 5
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var up loc=BP-2 type=lua boxed
    ; --- 'up' is captured by a nested closure: box it ---
    PUSH  R1 ; preserve value across __malloc
    MOV   R0, 1
    PUSH  R0 ; box size = 1 word
    CALL  __malloc
    IADD  SP, 1 ; clean up malloc argument
    POP   R1 ; restore value saved before __malloc
    MOV   [R0], R1 ; store initial value into the box
    MOV   [BP - 2], R0 ; slot now holds the box pointer
    ;@var g loc=BP-3 type=lua
;@src edge.lua:13
    MOV   R1, BOXED_NIL
    MOV   [BP - 3], R1 ; init local 'g'
    JMP   __g__l0_skip
__function_g__l0:
;@src edge.lua:13
    PUSH  BP
    MOV   BP, SP
    ISUB  SP, 14 ; Reserve stack for locals + spills
    ; --- Default implicit return value(s): nil until overwritten ---
    MOV   R0, BOXED_NIL
    ; --- 'up' is captured -- boxed access ---
    MOV   R1, [BP + 2] ; load box pointer
    MOV   R1, [R1] ; dereference box
    PUSH  R1 ; spill return value 0 (protect across later nested CALLs)
    POP   R0 ; restore return value 0
    MOV   R1, 1
    MOV   [RET_COUNT], R1 ; number of values returned
    JMP   __g__l0_return
    ; --- Re-establish Nil for IMPLICIT (fall-through) return ---
    MOV   R0, BOXED_NIL ; implicit return: fell off the end without 'return'
    MOV   R1, 0
    MOV   [RET_COUNT], R1 ; no values returned
__g__l0_return:
    MOV   SP, BP
    POP   BP
    MOV   R7, [SP] ; peek return address (don't consume it yet)
    IADD  SP, 2 ; discard return address slot + 1 upvalue word
    JMP   R7 ; manual return, stack now balanced

    ;@fn __function_g__l0 escapes=none
__g__l0_skip:
    ;; Build closure record for 'g__l0' (1 upvalue)
;@src edge.lua:13
    PUSH  R0 ; preserve caller's R0 across this closure's own malloc scratch
    MOV   R0, 4
    PUSH  R0
    CALL  __malloc
    IADD  SP, 1 ; clean up malloc argument
    MOV   R2, R0 ; closure record base
    IADD  R2, 1
    AND   R2, 0xFFFFFFFE ; even address
    MOV   R0, __function_g__l0
    MOV   [R2], R0 ; word 0: code address
    MOV   R0, 1
    MOV   [R2+1], R0 ; word 1: upvalue count
    POP   R0 ; restore caller's R0
    MOV   R3, [BP - 2] ; box pointer for captured 'up'
    MOV   [R2+2], R3
    MOV   R1, R2 ; box the closure record
    SHL   R1, -1 ; address / 2 (see above)
    OR    R1, BOXED_FUNCTION
    OR    R1, BOXED_CLOSURE_FLAG ; mark payload as a closure record, not raw code
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    MOV   [BP - 3], R1 ; store 'g'
;@src edge.lua:14
    MOV   R2, [BP - 3] ; load 'g'
    MOV   [BP -14], R2 ; REGSPILL: Store to spill slot
    MOV   R2, [BP -14] ; REGSPILL: Load from spill slot
    MOV   R0, R2 ; Prepare boxed target for validation
    CALL  __builtin_exec ; Validate and execute
    MOV   R1, R0 ; Store return value
    PUSH  R1 ; spill left operand (protect across possible nested CALL in right operand)
    MOV   R2, [BP - 1] ; load 'a'
    POP   R1 ; reload spilled left operand
    MOV   R0, R1
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __f_num_2
    ;@cold {
    MOV   R0, R1
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R1, R0
    ;@cold }
__f_num_2:
    MOV   R0, R2
    AND   R0, NAN_VALUE
    IEQ   R0, NAN_VALUE ; not a number?
    JF    R0, __f_num_3
    ;@cold {
    MOV   R0, R2
    CALL  __arith_coerce ; a numeric string -> its number
    MOV   R2, R0
    ;@cold }
__f_num_3:
    FADD  R1, R2
    PUSH  R1 ; spill return value 0 (protect across later nested CALLs)
    POP   R0 ; restore return value 0
    MOV   R1, 1
    MOV   [RET_COUNT], R1 ; number of values returned
    JMP   __f_return
    ; --- Re-establish Nil for IMPLICIT (fall-through) return ---
    MOV   R0, BOXED_NIL ; implicit return: fell off the end without 'return'
    MOV   R1, 0
    MOV   [RET_COUNT], R1 ; no values returned
__f_return:
    MOV   SP, BP
    POP   BP
    RET  

    ;@fn __function_f escapes=none
__f_skip:
;@src edge.lua:14
    JMP   __main_skip
__function_main:
;@src edge.lua:14
    PUSH  BP
    MOV   BP, SP
    ISUB  SP, 15 ; Reserve stack for locals + spills
    ; --- Default implicit return value(s): nil until overwritten ---
    MOV   R0, BOXED_NIL
    ;; Load and box address of the mangled function
;@src edge.lua:18
    MOV   R2, __function_f
    OR    R2, BOXED_FUNCTION ; Box as Function
    MOV   [BP -5], R2 ; REGSPILL: Store to spill slot
    ; --- Pushing explicit arguments Right-to-Left ---
    MOV   R3, 0x3F800000 ; 1
    PUSH  R3
    MOV   R2, [BP -5] ; REGSPILL: Load from spill slot
    MOV   R0, R2 ; Prepare boxed target for validation
    MOV   R13, 1 ; argument count (variadic ABI)
    CALL  __builtin_exec ; Validate and execute
    IADD  SP, 1 ; Clean up call arguments
    MOV   R1, R0 ; Store return value
    PUSH  R1 ; Pass 1: hold evaluated RHS value #0 until all targets are known
    POP   R1 ; Pass 2: retrieve evaluated RHS value for target #0
    ;@var r loc=BP-1 type=lua
    MOV   [BP - 1], R1 ; init local 'r'
__main_while_start_0:
;@src edge.lua:19
    MOV   R1, [BP - 1] ; load 'r'
    MOV   R2, 0x40400000 ; 3
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
    MOV   R1, [BP - 1] ; load 'r'
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
    MOV   [BP - 1], R1 ; store 'r'
    ;@scope }
    JMP   __main_while_start_0
__main_while_end_0:
;@src edge.lua:20
    WAIT 
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
;@src edge.lua:20
    PUSH  BP
    MOV   BP, SP
    ISUB  SP, 14 ; Reserve stack for top-level locals + spills
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
    MOV   [var_g], R0 ; nil-init global 'g'
    MOV   [var_r], R0 ; nil-init global 'r'
    ;; Load and box address of the mangled function
;@src edge.lua:21
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
__builtin_relcmp:
    RET
__globals_to_nil:
    RET
__malloc:
    RET
