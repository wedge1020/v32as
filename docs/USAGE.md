# Using v32as: from an .asm file to a cartridge

This page is a task-oriented walk through `v32as`, the drop-in Vircon32
assembler, and its linker `v32asld`. The exhaustive reference is the
[README](../README.md) (every option, every output file format); the
manual pages (`v32as(1)`, `v32asld(1)`, `v32as-language(7)`, `v32as-inc(3)`,
`v32as-control(3)`) cover the same ground in `man` form. Here you will find
the commands you actually type, in the order you will probably need them.

All the examples were run against the current build. Commands are shown
without a prompt; output is shown as it appeared (trimmed where noted).

---

## 1. What v32as is

`v32as` turns Vircon32 assembly (`.asm`) into a **VBIN**, the format of a
cartridge program. For the same source it writes the same bytes as the
official `assemble` tool. On top of that it adds:

* a preprocessor with macros, conditionals, loops and `%include <...>`;
* a few extra number forms, `.local` labels, and `MOV` with the internal
  registers `IP`, `IR`, `IV`;
* relocatable objects (`-C`) and a linker, `v32asld`;
* listings (`-T`), an interactive mode (`-i`), and debug outputs for the
  simulator (`-g`, `--map`);
* the cartridge definition directives (`%title`, `%texture`, `%sound`) and
  the XML and build scripts that go with them.

`--standard` switches every extension off ([section 11](#11-strict-compatibility---standard)).

Where it sits in the toolchain:

```
 game.asm ──v32as──> game.vbin ──┐
 gfx/*.png ──png2vircon──> *.vtex ├──packrom (game.xml)──> game.v32 ──> emulator
 snd/*.wav ──wav2vircon──> *.vsnd ┘
```

`v32as` replaces the `assemble` step. If your assembly comes out of a
compiler (`v32cc`, `v32lua`), point its build at `v32as` instead of
`assemble`; the debug options in section 8 read what those compilers write.

---

## 2. Install

### From a release zip

Unpack it anywhere. The layout is:

```
v32as  v32asld                  (v32as.exe, v32asld.exe on Windows)
include/v32as/v32as.inc         the %include libraries
include/v32as/control.inc
docs/v32as/README.md            with CHANGES.md, LICENSE, docs/*.md
docs/v32as/man/man1 man3 man7   the manual pages
```

`v32as` finds `include/v32as/` beside itself, so `%include <v32as.inc>`
works without any `-I` as long as the two stay together. Put the folder on
your `PATH` and check:

```
v32as --version
```

To read the manual pages from the unpacked tree, point `MANPATH` at
`docs/v32as/man`.

### From source

You need a C compiler, `flex` and `bison` (GNU `readline` is used for
line editing in the interactive mode when it is found).

```
make
make test        # optional
make install     # ~/bin, ~/share/v32as/lib, ~/man and ~/share/doc/v32as
```

`make install PREFIX=/usr/local` (or `make sysinstall`) uses
`PREFIX/bin`, `PREFIX/share/v32as/lib`, `PREFIX/share/man` and
`PREFIX/share/doc/v32as`. `DESTDIR`, `BINDIR`, `MANDIR`, `LIBDIR` and
`DOCDIR` are honoured, and `DEST_LANG=en|es|fr` (default `en`) picks the
language of the installed documents and of the `v32as(1)` and `v32asld(1)`
manual pages. The install directory of the
libraries is compiled into `v32as`, so `%include <v32as.inc>` works
wherever you install it. `make uninstall` removes it again.

---

## 3. Assemble a program

```
; hello.asm: clear the screen to blue, forever
_main:
    mov  R0, 0xFF0000FF         ; opaque blue (ABGR)
    out  GPU_ClearColor, R0
_frame:
    out  GPU_Command, GPUCommand_ClearScreen
    wait
    jmp  _frame
```

```
v32as hello.asm
```

This writes `hello.vbin` for a cartridge (base address `0x20000000`),
exactly what `assemble hello.asm` does. Things to remember about the
language: a label starts with an underscore, mnemonics and registers are
case-insensitive, and a comment starts with `;`.

Useful variations:

| Command | Effect |
|---|---|
| `v32as -o out/game.vbin game.asm` | Name the output. |
| `v32as -b bios.asm` | Assemble for the BIOS (base `0x10000000`). |
| `v32as -c game.asm` | Cartridge; this is already the default. |
| `v32as -O 0x3000 -o ram.bin code.asm` | Any base address (code that runs from RAM). |
| `v32as a.asm b.asm -o prog.vbin` | Several files form one program, in the order given. |
| `echo ' hlt' \| v32as - > out.vbin` | Read STDIN, write STDOUT. |
| `v32as -w game.asm` | No warnings. |
| `v32as -v game.asm` | Trace the encoding of every instruction (STDERR). |

Options may go before or after the file names. `-b`, `-c` and `-O` exclude
each other. Several input files share one preprocessor state (macros,
defines) and one symbol table.

**Reading STDIN.** With no input file, `v32as` reads STDIN only when it is
redirected or piped; it then writes the VBIN to STDOUT (it refuses to write
binary data to a terminal). Typing `v32as` alone at a terminal does not
wait for input: it prints the usage summary to STDERR and exits with status
2. For the interactive mode use `-i` ([section 5](#5-the-interactive-mode--i)).

**Warnings.** A number with a leading zero is octal in `v32as` but decimal
in the official assembler, so every such literal warns:

```
oct.asm:1:11: warning: '0777' is octal here, but the official assembler reads it as decimal (use --standard for that, or drop the leading zero)
```

Drop the zero, or see [section 11](#11-strict-compatibility---standard).

---

## 4. Listings and the preprocessor's output

`-T` prints the encoding of every instruction instead of writing a VBIN:

```
v32as -T -c hello.asm
```

```
OFFSET:      0x20000000
instruction: 0x4E000000
immediate:   0xFF0000FF
-----------------------
OFFSET:      0x20000002
instruction: 0x60000202
-----------------------
```

Add `-b`, `-c` or `-O ADDR` to choose the addresses shown; with none of
them (`-T`, or its synonym `-n`) the listing has no `OFFSET` lines, only
the instruction and immediate words.

`-E` prints the source after preprocessing, with line markers such as
`# 1 "hello.asm"` (`-P` leaves them out). It is the way to see what a macro does:

```
v32as -E -P game.asm
```

---

## 5. The interactive mode (`-i`)

`-i` (`--interactive`) starts a read-eval-print loop: every line you type
is assembled as soon as you press Enter, and its encoding is printed. It
takes **no input files**.

```
v32as -i
```

```
v32as 20261121-dev interactive mode: .help for commands, Ctrl-D to quit
v32as> mov R0, 5
instruction: 0x4E000000
immediate:   0x00000005
-----------------------
v32as> _x: iadd R0, 1
label:       _x = 0x00000002
instruction: 0x9A000000
immediate:   0x00000001
-----------------------
v32as> jmp _x
instruction: 0x0A000000
immediate:   0x00000002
-----------------------
v32as> .labels
0x00000002  _x
v32as> .dump
0x00000000  0x4E000000
0x00000001  0x00000005
0x00000002  0x9A000000
0x00000003  0x00000001
0x00000004  0x0A000000
0x00000005  0x00000002
v32as> .quit
```

The commands are:

| Command | Effect |
|---|---|
| `.help` | Show the commands. |
| `.labels` | List the labels defined so far. |
| `.dump` | Show every word assembled so far. |
| `.reset` | Forget everything assembled so far. |
| `.quit` | Leave (so does Ctrl-D). |

Labels, data directives and preprocessor directives work too; a `%macro`
or `%if` block is continued on further lines (the prompt changes to
`...>`) until it is closed. Addresses start at 0. Choose another base with
`-b`, `-c`, `-O ADDR` (or `-T`/`-n` for the base-less listing); the prompt
then shows the current address:

```
v32as -i -c
```

```
[0x20000000] > mov R0, 1
```

The options that make sense for a session combine with `-i`:
`-b -c -O -T -n`, `-D -U -I`, `-w`, `-v` and `--standard`. The ones that
write files or need a whole program do not, and are an error (status 2):
`-o -E -B -C -g -r --symbols --map -MD --rom-xml --build-scripts --v32x -s`,
and any input file.

---

## 6. The preprocessor and the macro libraries

Every line starting with `%` is a preprocessor directive. Besides the
official `%define`, `%ifdef`, `%include`, `%error` and `%warning`,
`v32as` has function-like macros, `%if`/`%elif`, `%assign`, `%eval(...)`,
`%macro` (statement macros), `%rep` loops and more. The summary is in
`v32as-language(7)` and the full text in the README
([Preprocessor](../README.md#preprocessor)).

Two ready-made libraries ship with `v32as` (see `v32as-inc(3)` and
`v32as-control(3)`):

* `v32as.inc`: `INC DEC ZERO NEG CLAMP SWAP PUSHALL POPALL WAIT_FRAMES`;
* `control.inc`: structured `IF` / `ELSE` / `ENDIF` and `WHILE` / `ENDWHILE`.

`%include <name>` looks in the `-I` directories first and then in the
library directory found beside `v32as` (or the one `make install` used),
so no `-I` is needed once the tool is installed. (`--standard` has none of
this.) A worked example:

```
; blink.asm: flash the screen every 30 frames
%include <v32as.inc>
%include <control.inc>

%define FLASH_EVERY 30

_main:
    ZERO    R1, R2              ; R1 = frame counter
_frame:
    INC     R1
    mov     R3, R1
    imod    R3, FLASH_EVERY     ; R3 = R1 mod 30
    IF      R3                  ; the condition is a register: non-zero?
        mov R0, 0xFF000000      ; black
    ELSE
        mov R0, 0xFFFFFFFF      ; white
    ENDIF
    out     GPU_ClearColor, R0
    out     GPU_Command, GPUCommand_ClearScreen
    WAIT_FRAMES
    jmp     _frame
```

```
v32as blink.asm
v32as -E -P blink.asm
```

The second command shows what the assembler really sees (the library's
comment and blank lines omitted here):

```
_main:
      mov   R1, 0
      mov   R2, 0
_frame:
    iadd  R1, 1
    mov     R3, R1
    imod    R3, 30     ; R3 = R1 mod 30
    jf    R3, __C1_else
        mov R0, 0xFF000000      ; black
    jmp   __C1_end
__C1_else:
        mov R0, 0xFFFFFFFF      ; white
__C1_end:
    out     GPU_ClearColor, R0
    out     GPU_Command, GPUCommand_ClearScreen
    wait
    jmp     _frame
```

Things to know:

* Macro names are case-sensitive and written in capitals; mnemonics are not.
* A macro checks its operands: `INC 5` is reported at your line, followed by
  a note that names the macro:

  ```
  inc5.asm:2: error: INC: the first operand must be a register
  inc5.asm:2: note: in expansion of macro 'INC' (defined at .../include/v32as/v32as.inc:21)
  ```
* Define your own with `%macro` ... `%endmacro`; `%%name` is a label private
  to each expansion:

  ```
  %macro COUNTDOWN reg, count
      mov   reg, count
  %%top:
      isub  reg, 1
      jt    reg, %%top
  %endmacro
      COUNTDOWN R1, 10
  ```
* Command-line definitions: `-DDEBUG`, `-DLEVEL=3`, `-UDEBUG`. A `%define`
  of the same name in the source warns and wins, unless you write
  `-D!LEVEL=3`.
* `-MD -MP` writes a Makefile rule listing the `%include` and `datafile`
  files, for build systems (`def.d` for `def.asm`).

---

## 7. Self-patching code: `IP`, `IR`, `IV`

`MOV` accepts the internal registers and expands to exactly one real
instruction, with no scratch register:

```
    mov R1, IP        ; address of the next instruction
    mov IP, R2        ; a jump
```

`MOV IR, Rn` and `MOV IV, Rn` overwrite the next instruction word or its
immediate at run time, so that code must run from RAM (`v32as` warns
otherwise). `MOV Rn, IV` is rejected. All of this is an extension, off
under `--standard`. The table is in the README
([Internal registers](../README.md#internal-registers-extension)).

---

## 8. Debug outputs for the simulator

### `-g`

```
v32as -g blink.asm
```

writes, next to `blink.vbin`:

* `blink.vbin.debug`, CSV `address,file,line[,label]`, one line per
  instruction (the first label at that address is the fourth column);
* `blink.vbin.sym`, the symbols with their final addresses.

```
0x20000000,blink.asm,8,_main
0x20000002,blink.asm,8
0x20000004,blink.asm,10,_frame
...
```

Plain `-g` is byte-identical to the official assembler's `-g program`
(absolute word addresses). `-g vbin` and `-g v32` give real byte offsets in
a `.vbin` or `.v32` file instead. Errors and the `.debug` file point at the
line **you** wrote, also through `%include` and macros (code a macro makes is
attributed to the line that invoked it).

Compilers that write `;@src FILE:LINE` marker comments (`v32lua -g`)
get their own lines in `.debug`, and `;@var` lines also produce `.vars`;
the formats are in the README ([Output files](../README.md#output-files)).

### `--map` (C programs from `v32cc`)

`v32cc -g` writes a CSV map of assembly lines to C lines
(`asmfile,asmline,cfile,cline[,function]`). Give it to `v32as` and the
`.debug` file names the C file and line instead:

```
v32as -g --map main.map main.asm
```

```
0x20000000,main.c,10,main
0x20000002,main.c,11,main
0x20000004,main.c,12,main
```

Instructions whose assembly line is not in the map are left out. With
`--map` and no `-g`, every error or warning is followed by a note naming the
C line it came from.

### Symbols and relocations

```
v32as -g -r game.rel --symbols game.sym game.asm
```

`--symbols FILE` writes `0xADDRESS,name,kind` per label (`F` code, `D`
data; add `--symbols-all` for file-local labels). `-r FILE` lists the words
that hold an address, `index,0xVALUE`, so a loader can relocate a module.

---

## 9. Building a ROM: `%title`, `%texture`, `%sound`

A cartridge is the program plus the textures and sounds it uses, packed by
`packrom` from an XML definition. `v32as` can take both from the source:

```
%title   "Space Rocks"
%version 1.2
%texture TEX_FONT "gfx/font.png"
%texture TEX_SHIP "gfx/ship.png"
%sound   SND_BOOM "snd/boom.wav"

_main:
    mov R0, TEX_SHIP
    out GPU_SelectedTexture, R0
    mov R1, SND_BOOM
    out SPU_SelectedSound, R1
    hlt
```

`TEX_SHIP` and `SND_BOOM` become the entry numbers (textures and sounds
are numbered separately, from 0, in order of first appearance), so the
program and the cartridge always agree. The files are looked for next to
the source, then in the `-I` directories.

### The definition

```
v32as game.asm --rom-xml game.xml
```

writes `game.vbin` and `game.xml`:

```
<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<rom-definition version="1.0">
    <rom type="cartridge" title="Space Rocks" version="1.2" />
    <binary path="game.vbin" />
    <textures>
        <texture path="font.vtex" />
        <texture path="ship.vtex" />
    </textures>
    <sounds>
        <sound path="boom.vsnd" />
    </sounds>
</rom-definition>
```

Paths are relative to the XML. A `.png` or `.wav` is named as the `.vtex`
or `.vsnd` that `png2vircon` / `wav2vircon` make from it, which you then
place next to the XML.

### Build scripts

```
v32as --build-scripts game.asm
```

writes `Make.sh` and `Make.bat` (and `game.xml`, `gameOpt.xml`) beside the
source, and assembles as usual. Run the script to rebuild everything:

```
./Make.sh
```

It runs, in order: the optimizer (`v32opt`, only when installed; otherwise a
note says it is skipped), `v32as`, `png2vircon` and `wav2vircon` for each
resource, and `packrom game.xml -o bin/game.v32`. It ends with
`BUILD SUCCESSFUL` or `BUILD FAILED`. Paths inside are relative to the
script, so it can be run from anywhere. `--build-scripts=sh` writes only
the shell script. Tools that are not on `PATH` can be named in `V32AS`,
`V32OPT`, `PACKROM`, `PNG2VIRCON`, `WAV2VIRCON`; `NO_OPT=1` skips the
optimizer.

A script is only replaced if it carries the line `Generated by v32as`; a
hand-written `Make.sh` (or one from `v32cc`) is left alone with a warning.

These options need a VBIN written to a file: not with `-C`, `-T`, `-n`,
`-E`, `-O`, `--v32x` or STDIN, and they are errors with `--standard`.
Details: README ([Cartridge definition and build
scripts](../README.md#cartridge-definition-and-build-scripts)).

---

## 10. Separate assembly and linking: `-C` and `v32asld`

Assemble each file to a relocatable object, then link:

```
; a.asm
global _start
extern _helper
_start:
    mov R0, 3
    call _helper
    hlt
```

```
; b.asm
global _helper
_helper:
    iadd R0, 1
    ret
```

```
v32as -C a.asm
v32as -C b.asm
v32asld -g -r prog.rel --symbols prog.sym -o prog.vbin a.v32ob b.v32ob
```

`global` and `extern` (plain names, no `%`) say which labels are shared; any
other label is local to its file. Objects are placed in command-line order,
so the first word of the first object is the entry point (`--entry _name`
puts a `jmp` in front when you want another). The result is identical to
assembling the sources joined into one file, and `-g` writes the same
`.debug`. The base is the linker's job: `-b`, `-c` and `-O ADDR` go to
`v32asld`, not `v32as -C`.

An unresolved name is reported once per symbol:

```
v32asld: error: undefined reference to `_helper' (first used in a.v32ob)
```

More: `v32asld -A libb.a b.v32ob` makes an archive (then name it among the
inputs, or use `-L DIR -l b`); `v32asld -R -o ab.v32ob a.v32ob b.v32ob`
joins objects into one object. Both are in `v32asld(1)`; the object format
is in `docs/OBJECT.md`.

---

## 11. Strict compatibility: `--standard`

```
v32as --standard game.asm
```

turns every extension off, so you can check that a program assembles with
the official `assemble` and get its exact behaviour: no binary or `h`-suffix
numbers, no `IP`/`IR`/`IV`, only the stock preprocessor directives (no
`%if`, `%macro`, function-like macros, `<file>` includes or `-I`),
`label+N` is an error, one input file only, `-v` prints the official stage
messages, and a leading zero means decimal:

```
mov R0, 0777
```

assembles to `0x309` (777) with `--standard` and to `0x1FF` (511, octal)
without it. The complete table is in the README
([`--standard`](../README.md#--standard-the-official-assembler-exactly)).

---

## 12. Exit status

| Status | Meaning |
|---|---|
| 0 | Success (warnings do not change it). |
| 1 | An assembly or preprocessor error, or an input file that cannot be read. |
| 2 | A usage error (bad or conflicting options, no input file at a terminal), or an output file that cannot be written. |

`v32asld` uses 1 for a link error and 2 for a usage error, a bad object or
archive, or a missing `-l` library.

---

## 13. Troubleshooting

Diagnostics have the form `file:line[:column]: error: message`. Errors
inside a macro are reported at the invocation, followed by a note that names
the macro and where it is defined.

| Message | Cause and fix |
|---|---|
| `label 'main' must start with an underscore` | Every label starts with `_`: write `_main:`. |
| `unknown identifier 'R16'` | Not a register, mnemonic or port name. Registers are `R0`..`R15` (`CR SR DR BP SP` are `R11`..`R15`). With `--standard`, `IP`, `IR`, `IV` are unknown too. |
| `undefined label '_nowhere'` | A jump or `mov` uses a label that is never defined. In a program split over objects, mark it `extern` and link. |
| `duplicate label '_main'` | Two sources define it. Several input files form **one** program, so each label may be defined once (assemble them separately with `-C` if both need `_main`). |
| `syntax error, unexpected "','", expecting end of line` | Too many operands for the instruction. |
| `cannot open include file "nope.inc"` | Not found next to the including file or in `-I` dirs. For `%include <v32as.inc>` check that `include/v32as/` sits beside `v32as`, or add `-I DIR`. |
| `unterminated %if block` | A `%if` / `%ifdef` without `%endif`. (A warning only under `--standard`.) |
| `'label +/- integer' is not part of the official assembler's language` | An extension used under `--standard`. |
| `'0777' is octal here, but the official assembler reads it as decimal` | Drop the leading zero, or use `--standard`. |
| `--origin cannot be combined with --bios or --cart` | Give only one of `-b`, `-c`, `-O`. |
| `--interactive cannot be combined with ...` | See [section 5](#5-the-interactive-mode--i). |
| `-g needs binary output to a file` | `-g` and `-r` need a VBIN written to a file (`-o FILE` or a single input file), not a listing, `-E`, `-C` or STDOUT. |
| `--rom-xml and --build-scripts need a BIOS or cartridge VBIN written to a file` | Remove `-C`, `-T`, `-n`, `-E`, `-O`, `--v32x`, or give `-o FILE`. |

If a macro does something unexpected, look at `v32as -E -P file.asm`. If
`v32as` seems to hang, it is reading STDIN: pipe or redirect the source, or
give a file name. Running it with no arguments at a terminal prints the
usage text instead.

---

## Where next

* [README](../README.md): every option, output file format, and the whole
  preprocessor.
* `man v32as`, `man v32asld`, `man v32as-language`, `man v32as-inc`,
  `man v32as-control`.
* `docs/PREPROCESSOR.md` (design notes), `docs/OBJECT.md` (object and
  archive formats), `CHANGES.md`.
