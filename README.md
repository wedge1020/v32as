# v32as — Vircon32 assembler in C (flex + bison)

A C port of  the bash `v32as`. Same job (assemble  Vircon32 assembly to a
VBIN, with a listing/verbose/step mode  and an interactive REPL), but the
lexer  is flex,  the parser  is bison,  and symbols/fixups  live in  hash
tables instead of bash arrays and `bc`.

```
    make          # builds bin/v32as  (needs flex, bison, cc; readline optional)
    make test     # differential/golden tests (including .debug files) (sh + od + cmp only)
    make install  # copies to ~/bin
    make archive  # ../v32as-project.zip, without build products
```

Layout: `src/` sources (flex, bison, C), `inc/` headers, `lib/` macro libraries for `%include`, `docs/`
design notes, `tests/`, `experiments/`; `make` puts generated files in `build/` and the tool in `bin/`.

## Usage

```
    v32as [-b|-c|-O ADDR] [-o FILE] [-g[MODE]] [--map FILE] [-r FILE] [-w] [-D NAME[=VAL]] [-U NAME] [-I DIR]
          [--standard] [-T|-n] [-E [-P]] [-s] [-v] [FILE...|-]
```

By default `v32as FILE.asm` does what the official `assemble FILE.asm` does: it writes `FILE.vbin`,
a VBIN for a cartridge. (Reading STDIN, the output goes to STDOUT.)

- `-o FILE` names the output. `-b/--bios` assembles for the BIOS (0x10000000); `-c/--cart` for a
  cartridge (0x20000000, the default); `-O/--origin ADDR` for any base address.
- `-T/--text` prints a human-readable listing (the instruction encodings) instead of writing a VBIN.
  Its addresses start at 0 unless `-b/-c/-O` is given. `-n/--none` is `-T` with no base address.
  (`-B/--binary` still exists, and only says what is already the default.)
- `-w/--no-warnings` inhibits all warnings.
- `-g/--debug[=MODE]` also writes `FILE.debug` (CSV `address,file,line[,label]`, one line per instruction) next to the output file. Plain `-g` is byte-identical to the official assembler's `-g program` (absolute word addresses, e.g. `0x20000000`). `-g vbin` / `-g v32` give real byte offsets in the .vbin (0x0C header) or .v32 (0x8C header) file; the official tool's versions of those two print wrapped-around values. A source file literally named `program`, `vbin` or `v32` must be written `./program`.
- `--map FILE` (with `-g`): FILE is the compiler's debug file (`asmfile,asmline,cfile,cline[,function]`, as `v32cc -g` writes it). The `.debug` file then has the same layout, but each instruction names the C file and line (and the function as the label column), which is what v32sim's C overlay reads. Instructions whose assembly line is not in FILE are left out.
- `-r/--relocs FILE` writes `index,0xVALUE` for every word that holds an address resolved from a label (code or data, `label+N` included) or computed from the instruction's own address (`MOV Rn, IP`, the `IR`/`IV` patch addresses). Plain numbers that happen to look like addresses are never listed, so a loader can relocate a module by adding the base difference to those words.
- `-s/--step` pauses per statement, `-v/--verbose` shows the encoded bit fields
- no files and a terminal on stdin starts the REPL: `.help .labels .dump .reset .quit`
- `-` reads stdin; the legacy bare words (`bios`, `cart`, ...) still work
- Preprocessor options: `-D`, `-U`, `-I`, `-E`, `-P` (see below).

### Label plus constant

Wherever a label is accepted as a value, `label + integer` or `label - integer` is too (one label, one
constant): `mov R0, _tab+2`, `mov R1, [_tab+3]`, `mov [_tab-1], R1`, `jmp _f+4`, `pointer _tab+1, _end`. It is
resolved like a plain label (forward references included). `--standard` refuses it.

### `--standard`: the official assembler, exactly

`--standard` turns every extension off, for a drop-in replacement of `assemble` (and for checking that a
program is portable to it):

| area | in `--standard` |
|---|---|
| numbers | `0b101`, `FFh`/`2Ah`, `#42` and `0X1F` are errors. A leading zero is **decimal** (`0100` is 100), as the official tool reads it |
| names | `IP`/`IR`/`IV` (internal registers) and `zstring` do not exist |
| directives | only the stock ones, spelled in lowercase; no `%if %elif %assign %eval %includeonce`, no function-like macros (`%define F(x) ...` defines the plain macro `F` whose value starts with `(x)`), no `.local` labels, no `__FILE__`-style symbols |
| `%define` | a value that contains its own name, or a `%`, is an error |
| `%include` | `"file"` only, relative to the including file only (no `-I`, no `<file>`) |
| operands | `label +/- integer` is an error |
| `datafile` | the path is used as given (not looked up next to the source file first) |
| command line | one input file only; `-s` does nothing; `-v` prints the official stage messages |
| unterminated `%if` | a warning (the official tool accepts it silently) |

Without `--standard`, the same program is accepted too, with one trap to know about: a number with a
leading zero is **octal** here (`0777` is 511) but **decimal** in the official assembler. `v32as` warns
about every such literal; use `--standard`, or drop the leading zero.

## Preprocessor

Source is preprocessed before it is assembled (`-E` shows the result). Design notes and roadmap are in
`docs/PREPROCESSOR.md`. A directive is a line whose first non-blank character is `%`; directive names are
case-insensitive, macro names are not. Macros are replaced only in code: never inside strings,
character literals, comments or numbers.

### Everything the official assembler has

```
%define NAME tokens...      %undef NAME
%ifdef NAME   %ifndef NAME  %else   %endif
%include "file"             %error "message"   %warning "message"
```

`%define NAME value` is expanded where it is *used* (late binding), and a macro is never re-expanded
inside its own expansion. `%include` paths are relative to the including file's directory (limit: 20
levels). Skipped `%ifdef` blocks drop every line, including unknown directives. An unterminated block is
an error (the official tool lets it pass; `--standard` does too, with a warning).

### Extensions

- **Function-like macros**: `%define MUL(a, b) ((a) * (b))`, used as `MUL(3, 4)`. Arguments are expanded
  before substitution. `#x` makes a string of the argument, `a ## b` joins two tokens
  (`%define LAB(n) _label_ ## n`), `...` / `__VA_ARGS__` take the remaining arguments (and `, ## __VA_ARGS__`
  drops the comma when there are none). `#` followed by a digit stays the `#42` number syntax. A name
  followed by `(` is only an invocation when the macro is function-like.
- **`%if expr`, `%elif expr`, `%elifdef`, `%elifndef`**, `%else`, `%endif`.
- **Expressions** (`%if`, `%assign`, `%eval`): integers (32-bit, wrapping) and floats (32-bit), all the
  number forms the assembler reads, `defined(NAME)`, `true`/`false`, the port and constant names the
  assembler knows, operators `( ) + - ! ~ * / % << >> < <= > >= == != & ^ | && || ?:` with C precedence
  (`&&`, `||` and `?:` do not evaluate what they skip), and the functions `int() float() abs() min() max()
  floor() ceil() round()`. An operation with a float operand is a float operation (`7/2` is 3, `7/2.0` is 3.5).
- **`%assign NAME expr`** computes a value now: `%assign N N + 1`. (`%eval NAME expr` is the same.)
- **`%eval(expr)`** anywhere in a line is replaced by the value: `mov R0, %eval(WIDTH * 2 + 1)` becomes
  `mov R0, 641`. This is what lets symbols take part in arithmetic, which `%define MATH A+B` cannot do:
  the assembler has no expressions, so the sum has to exist as a number in the source. Floats are written
  back in the shortest form that reads back as the same float.
- **`.local` labels**: after a global label `_draw:`, the name `.loop` means `_draw__loop`
  (so `.loop:` defines it and `jt R1, .loop` uses it).
- **Symbols**: `__FILE__`, `__LINE__`, `__COUNTER__`, `__INCLUDE_LEVEL__`, `__V32AS__` (version date as a
  number), `__BASE__` (first address), and one of `__BIOS__`, `__CART__`, `__ORIGIN__`, `__NONE__`.
  A `%define` of the same name wins.
- **Includes**: `%include <name>` searches only the `-I DIR` directories; `%include "name"` tries the
  including file's directory first and then the `-I` directories. `%includeonce` in a file makes any later
  `%include` of it a no-op.
- **Command line**: `-D NAME[=VALUE]` (value defaults to 1), `-U NAME`, `-I DIR`, `-E` (print the
  preprocessed source with `# line "file"` markers, `-P` without them).

- **`%idefine`, `%xdefine`, `%ixdefine`**: a case-insensitive define; a define whose value is expanded
  now rather than where it is used; both.

### Macros that write instructions

```
%macro CLAMP reg, lo=0, hi=255        ; named parameters, with defaults
    imax  reg, lo
    imin  reg, hi
%endmacro

%macro COUNTDOWN reg, count
    mov   reg, count
%%top:                                ; private to each expansion (becomes __M<serial>_top)
    isub  reg, 1
    jt    reg, %%top
%endmacro

    CLAMP R3, , 100                   ; invoked like an instruction (labels may come first)
    COUNTDOWN R1, 10
```

- **`%macro NAME params` ... `%endmacro`**. Parameters are `name`, `name=default` (a default makes it
  optional; an empty argument takes the default) and a last `rest...` (or `...`) that takes the remaining
  arguments. `%macro NAME 2`, `1-3` or `1-*` only fixes how many arguments there are. Inside the body a
  parameter name, or `%1` `%2` ..., stands for its argument text, `%0` for the number of arguments and `%*`
  for all of them. Arguments are split at commas outside `()` `[]` `{}` and quotes; `{a, b}` passes a comma
  as part of one argument. A macro may be defined several times if the argument counts differ; `%unmacro NAME`
  removes it.
- A macro name is **case-sensitive** (`%imacro` makes it case-insensitive) and **wins over an instruction** of
  the same name, with a warning unless `%pragma shadow` (everything) or `%pragma shadow NAME, ...` allows it.
- **`%rep count` ... `%endrep`** repeats its body (at most 1,000,000 times); `%@` is the iteration number.
  `%exitrep` leaves the loop. Loops nest and may contain macros and conditionals.
- **`%exitmacro`** leaves the macro (also from inside `%if` or `%rep`). **`%rotate n`** rotates what `%1`
  `%2` ... refer to (left for positive `n`), the way to walk a variadic list:
  `%rep %0` / `push %1` / `%rotate 1` / `%endrep`.
- **`%%name`** is a label private to one expansion of a macro (one per iteration for a `%rep` outside a macro;
  inside a macro it is the macro's, so use `%%name_%@` for one label per iteration).
- **Operand tests**, usable as `%ifXXX` and `%elifXXX`, and negated with `n` (`%ifnreg`):
  `reg` (a CPU register), `sysreg` (`IP` `IR` `IV`), `num` (a constant expression; a bare port name is not
  a number), `str` (a string literal), `label` (`_name` or `.name`), `port` (an I/O port name),
  `idn a, b` / `idni a, b` (the same text, exactly or ignoring case), `empty`, `macro NAME [count]`, `ctx NAME`.
  The text is macro-expanded first. This is what lets one macro choose between `mov` and `in` according to
  its operands, and report a bad operand with a real message (`%error "INC wants a register, got %1"`).
- **Strings**: `%strlen N "text"`, `%substr S "text", start [, length]` (start counts from 1), `%strcat S "a",
  "b"`, `%defstr S tokens` (the expanded tokens as a string).
- **Contexts**: `%push name` ... `%pop` (`%repl name` renames). `%$x` is a name private to the innermost
  context (`%$$x`: the one below), which is what lets macros like `IF` / `ELSE` / `ENDIF` share labels.
  `%ifctx name` tests the innermost context.
- **`%pragma once`** (same as `%includeonce`), `%pragma shadow ...`.

`lib/v32as.inc` has ready-made `INC DEC ZERO NEG CLAMP SWAP PUSHALL POPALL WAIT_FRAMES` (all check that their
operands are registers) and `lib/control.inc` has `IF` / `ELSE` / `ENDIF` and `WHILE` / `ENDWHILE`; use them with
`-I lib` and `%include "v32as.inc"`.

A macro expansion is limited to 64 levels, and a run to about 4 million replayed lines (runaway `%rep` or
recursion stops with an error). Errors from inside an expansion are reported at the line of the invocation
with a `note: in expansion of macro 'X' (defined at file:line)`; `-g` also attributes the generated code to
the invocation.

Errors and the `-g` file point at the file and line you wrote, also through `%include` and macros (on a
line where a macro expanded, the column is left out). Multiple input files share one preprocessor state
(macros, defines, contexts).

## Verification

Output is  byte-identical to the  official Vircon32 assembler  for: every
opcode in every  legal operand form, all I/O ports  and port values (BIOS
and CART), and all data directives. `tests/golden/*.hex` were produced by
the official assembler.

## Differences from the bash version

- The VBIN header carries the real word count (bash hard-coded 155).
- Numeric ports in `IN`/`OUT` work (bash bug); unknown/truncated ports warn.
- Integer literals given to float-class ops are converted to float; ATAN2/POW take registers only (as official).
- `[R+0]` is encoded as `[R]` (as official).
- Data directives: `integer`, `float`, `pointer`, `string`, `zstring`, `datafile`.
  One word per element; `string` is one word per character plus a terminating 0.
  `zstring` is byte-packed: four characters per word (first character in the low byte),
  zero-terminated, last word zero-padded. (`zstring` is not in the official assembler;
  its golden file is hand-verified, not official output.)
  `datafile` reads little-endian words, relative to the source file's directory first.
- Added: `true/false`, character literals, `0b`, octal, `#dec`, `h` suffix numbers;
  labels and a statement may share a line; mnemonics/registers are case-insensitive.

## Internal registers (extension)

`IP`,  `IR`, `IV`  are not  CPU-addressable, but  `MOV` accepts  them and
expands to exactly one real instruction, using no hidden scratch register
(A = address of the expansion):

| source               | expands to              | meaning                                   |
|----------------------|-------------------------|-------------------------------------------|
| `MOV Rn, IP`         | `MOV Rn, A+2`           | address of the next instruction           |
| `MOV Rn, IR`         | `MOV Rn, <this word>`   | encoding of the instruction being executed|
| `MOV IP, Rn / addr`  | `JMP Rn / addr`         | jump                                      |
| `MOV IR, Rn`         | `MOV [A+2], Rn`         | overwrite the next instruction word       |
| `MOV IV, Rn`         | `MOV [A+3], Rn`         | overwrite the next instruction's immediate|

`MOV Rn, IV` is rejected (self-referential); IR/IV cannot be written from
an immediate. The IR/IV writes patch code  at run time, so that code must
run from RAM (the assembler warns in --bios/--cart mode). `MOV Rn, IP` in
--none mode is relative to address 0 (warns).
