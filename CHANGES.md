# Changes

The development history of `v32as` and `v32asld`, by round, oldest first. The current behaviour of everything
listed here is described in `README.md`; this file records what each round (a set of requests from the
projects that use the assembler) added or fixed. Before round 9 the assembler had the features described in the
README's "History and compatibility" section and the preprocessor phases 1 to 3 of `docs/PREPROCESSOR.md`.

## 2026-11-05: `OUTPUT.sym` (SA-3), `make version`

- **Version 20261105-dev**, stamped with the new `make version [V=... VDATE=...]` (the `VERSION` macros of `v32as` and `v32asld` and the `.TH` line of every man page). `make install` and `make sysinstall` (PREFIX=/usr/local) were already there, with `uninstall`.
- **A resolved symbol file from `-g` (SA-3).** `v32as -g` (not `--standard`) and `v32asld -g` also write `OUTPUT.sym` (`prog.vbin.sym`) in the
  format of v32cc's `NAME.asm.sym`: `kind,name,address,words,cfile,cline` with F (function: top-level label followed by code, name without
  `__function_`, size to the next top-level label, position of its first instruction), R (data label) and V (`%define global_NAME` with words 0
  = not known, `%define var_NAME` of v32lua with words 1). Addresses are always CPU addresses, whatever the `-g` mode. The linker writes F and R
  only (an object holds no `%define`s) and takes a code label for a function when a debug record names it. Details: README, "The .sym file".
  Nothing existing changes except that a new file appears next to `.debug`.
- **A `;@var` outside any function lasts to the end of the program.** v32cc's globals (`;@var NAME loc=global_NAME`, written at the top) used to
  end at the first `;@src -`; now only variables of a function (or of a block in it) end there. `tests/markers/c_globals.asm`.

## 2026-11-04: functions of C producers, `;@lang`, symbol locations in `.vars`

- **Function ends for non-Lua producers (SA-1).** `;@lang X` (a comment line of its own, the first one counts) is now read. With a
  language other than `lua`, a new top-level `__function_NAME:` label ends the previous function (its `;@var` ranges and blocks end at that
  label's address, where the FUNCTION column of `.debug` changes), so `v32cc` (`;@lang c`) needs no `;@src -` or `;@fn` after each body. A
  `;@fn` above a label stays a hint for every language. Without `;@lang`, or with `lua`, the behaviour is the same as before.
- **Symbol locations in `.vars` (SA-2).** `;@var NAME loc=SYMBOL` with a label, `label+N`, or a `%define`d name that is a number (v32lua's
  `var_NAME`) or a label is written as `@0xADDRESS` in the LOC column when known at the end of the assembly (a label in the style of the
  `-g` mode, a `%define`d number as it is); registers, `@0x...` and `-` are unchanged; an unknown symbol stays text and warns under
  `-Wmarkers`. With `-C` a `%define`d number is resolved by `v32as`, a label by `v32asld -g` (the object holds the text), giving the same
  file as the joined source.
- **`# lang X` (SA-4).** With `;@lang X` in marker mode the first line of `NAME.vbin.debug` is the comment `# lang X`, and the same line
  starts `NAME.vbin.vars`. Readers must skip lines that start with `#`. Not with `--map`, plain `-g` or `--standard`. **The `.debug` of a
  source that has `;@lang` therefore changes by that one line** (the goldens of `tests/markers/bench*.debug` got it).
- **Object format version 4** (`docs/OBJECT.md`): a version 3 object plus the language and two flags, written only for a source with a
  `;@lang` (in marker mode or with `;@var` lines). Versions 1 to 3 are still read, and objects without `;@lang` are byte-identical to before.

## 2026-10-27: memory use (so that v32as runs on v32nix)

A named file goes through the preprocessor a line at a time, the preprocessor's output is scanned in place, each
file-local label is remembered once, and the debug records and the address list are kept only for `-g`, `-r`, `-C`
and `--symbols-all`. The output is the same.

## Round 9 (answers to V32AS-PROPOSAL)

- **`datafile` search (VA-1)**: absolute path; the directory of the file holding the directive; each `-I DIR`;
  the current directory. The error lists the places tried: `cannot open data file "x" (looked in out/, ../data/, .)`.
  `--standard` keeps the two old places (no `-I`, as for `%include`).
- **`--symbols FILE` (VA-2)**: `address,name,kind` per label, sorted by address; kind `F` (code follows), `D` (data
  follows, or nothing). `--symbols-all` adds file-local labels as `function.local` and every `%define global_NAME number`
  as kind `V`, and every `DATASYM+N` the program refers to (see `datasym`) as `N,DATASYM+N,V`: the offset in the
  address column. Not available with `-C` (use `v32asld --symbols`).
- **`-D` against `%define` (VA-3)**: a `%define` of a `-D` name warns (the file still wins); `-D!NAME=VALUE` or
  `--force-define NAME=VALUE` makes the command line win. A `%define` that changes a macro's value warns
  (`-Wredefine`, on by default; `-Wno-redefine`, `-w` and `--standard` silence it).
- **`--map` (VA-4)**: file names are compared exactly first, then by base name (the longest shared directory tail wins),
  so `main.asm` in the map matches `out/kernel/main.asm`. If no instruction matches at all, a warning names the
  names found in the map.
- **v32asld (VA-5)**: `--symbols FILE`; `-A FILE` makes an archive (`V32-VARC`) of objects; archives given as operands or
  with `-L DIR -l NAME` (libNAME.a) are mined after the objects, a member being linked only when it defines a symbol still
  undefined; `--entry SYMBOL` puts a two-word `jmp SYMBOL` first unless the symbol is already the first word;
  `--v32x --data N [--entry SYMBOL|OFFSET]` writes a V32X executable (header + image linked at 0x100000).
  ``undefined reference to `x' (first used in a.v32ob)``, once per symbol.
- **`-MD -MMD -MP -MF FILE -MT TARGET` (VA-6)**: the sources, `%include` files and `datafile` files read, as a Makefile rule
  (default file: the output name with `.d`).

## Round 10 (answers to the V32DOS proposal)

- **Bug fixed (VA-1)**: a symbol's `datasym` flag was never initialised, so on larger inputs ordinary labels read
  as datasyms and `-r` listed code relocations as `,D` (the V32DOS `byteaddr` module: `call __function_main`).
  `tests/reloc/` runs the reduced repro with the heap poisoned (`MALLOC_PERTURB_`).
- **`datasym` in objects (VA-1)**: `v32as -C` accepts `datasym`, the object records it (format version 2) and
  `v32asld -r FILE` (also `--emit-relocs FILE`) writes the third column, `,C` or `,D`, as `v32as -r` does. A datasym
  has the value 0 in every object and is not moved by the link.
- **`-C` with `--map` (VA-2)**: the object stores the C file and line (and the function) of each instruction in place
  of the assembly ones; instructions the map does not know are left out, as in `-g --map`. `v32asld -g` then writes
  the C-level `.debug` file, the same as `v32as -g --map` would on the joined source. (Object format version 2.)
- **Partial link (VA-3)**: `v32asld -R -o ab.v32ob a.v32ob b.v32ob` (not `-r`, which is the relocation list) joins
  objects into one object. Globals stay global, an `extern` another input defines becomes a reference to it, the
  others stay `extern`; relocations, datasyms and debug records are kept, so linking `ab.v32ob` equals linking
  `a.v32ob b.v32ob`. Archives and `-l` work as in a full link. It refuses `-b -c -O -g -r --symbols --entry --v32x`.
- **Diagnostics through `--map` (VA-4)**: with `--map FILE` (no `-g` needed) every error and warning of the
  assembler or the preprocessor is followed by `a.c:123: note: the assembly line above was generated from here`
  (the function is named when the map has it), or, for a line with no entry of its own, by
  `... it follows the code generated from here` naming the nearest earlier mapped line.
- **The `-r` file (VA-5)**: its format is documented in the README ("The -r (relocation) file").

## Round 11 (answers to PROPOSAL-v32as: `;@src` markers)

- **`-g` reads `;@src` markers (no `--map` needed)**: a comment line of its own, `;@src FILE:LINE` (or `FILE:LINE:COL`;
  `;@src -` for "no source"), as `v32lua -g` writes it (see V32DBG-SPEC). When the source has any marker,
  `NAME.vbin.debug` gets `ADDRESS,FILE,LINE[,FUNCTION]` from them: one entry per marker, at the address of the first
  instruction after it (so two consecutive markers naming the same line give two entries; markers with no instruction
  between them give one, for the last). An instruction no marker starts has no entry (those before the first marker have
  none at all). FILE is split from LINE at the last `:` (or the last two when both end in digits), so a drive letter is
  fine. The marker must be the first thing on its line (a trailing `;@src ...` after an instruction is only a comment).
  Markers survive `%include` and macros as the comments they are; the file name in the marker is what is written.
- **`;@src -` (v32sim request)**: the first instruction after it gets the entry `ADDRESS,-,0` (file `-`, line 0, no
  function), so the sim knows where the last source line ends and the runtime begins. A malformed marker counts as
  `;@src -`. Instructions before the first marker still have no entry.
- **FUNCTION**: the function the instruction is in (rules for where functions start and end came in round 12), written
  `NAME` (`__function_main:` -> `main`, `__function_f__l0:` -> `f__l0`); no fourth column outside functions. Every entry
  of a function has it, not only the first. Unlike plain `-g` (which writes the first label at the address) no other
  label is written in marker mode.
- **`--map FILE` still works**: for an instruction the map names, the map's entry is written; every other marker still
  gets its own. `v32cc` writes no markers today, so its output does not change.
- **`-C`**: the object stores the marker entries in place of the assembly lines (format version 2; function word
  `0xFFFFFFFE` means "no function, and `v32asld -g` must not add a label"), so `v32asld -g` writes the same file as
  `v32as -g` on the joined source. The `--map` precedence is the same.
- **`--standard`**: markers are comments; `-g` writes the assembly lines as before.
- **`-Wmarkers`** (off by default; `-Wno-markers`): warn about a `;@src` whose `FILE:LINE` is malformed (no `:`, a line that
  is not a number >= 1, nothing after `;@src`). Such a marker is treated as `;@src -` (entry `-,0`): no source is better
  than a wrong line. `-w` silences it.
- Tests: `tests/markers/` (golden `.debug` files; markers and `--map` of the same lines give the same file; `--map` wins;
  `-C` + `v32asld -g` and a partial link give the same file; the program is byte-identical with the markers removed).

## Round 12 (answers to V32DBG: `;@var`, `;@scope`, functions)

- **`NAME.vbin.vars`** (with `-g`, next to `NAME.vbin.debug`; only when the source has `;@var` lines; an old file is removed
  otherwise): the locals as address ranges, which only the assembler can compute. Addresses as in `.debug` (`-g`, `-g vbin`,
  `-g v32`), END being the first address that is out of scope. One CSV line each, in order of START (a block before its
  variables): `scope,START,END,DEPTH[,FUNCTION]` and `var,START,END,NAME,LOC,TYPE,FLAGS,DEPTH[,FUNCTION]`. `LOC` is the
  `loc=` as written (`BP-3`, `BP+2`, `R5`, `@0x20003000`, a symbol; `-` for `optimized-out`), `TYPE` the `type=` (`-` if
  none), `FLAGS` the bare words after it (`boxed`, `optimized-out`, joined by `+`; `-` if none; `key=value` words other than
  `loc`/`type` are ignored). `DEPTH` is 0 for the function's own scope and 1, 2, ... for blocks in it, so the sim can pick
  the innermost of two variables with the same name. The scope lines are the `;@scope {` ... `}` blocks.
- **Ranges.** A `;@var` is in scope from the first instruction after it. It ends at the `;@scope }` of the block it was
  declared in, or, with no block, at the end of the function; or at another `;@var` of the same name in the same block (a
  new home, e.g. the optimizer's `loc=R5`, or `;@var NAME optimized-out`). A same-name `;@var` in a nested block shadows and
  does not end the outer one. Empty ranges (`do end`) are left out. An unbalanced `;@scope }` is ignored and an open block
  is closed with its function (both warn under `-Wmarkers`).
- **Functions** (also for the `FUNCTION` column of `.debug`): a function starts at a `__function_NAME:` label and ends at the
  line `;@fn __function_NAME ...` that the Lua compiler writes after its body, at a `;@src -`, or at the end of the program.
  Functions nest (a Lua closure's body sits inside its parent's code, and the parent's code after it is the parent's again),
  and code between functions or after `;@src -` (global initialisation, the runtime) has no function. A `;@fn` written above
  a label (the runtime's `clobbers=` form) is not an end. `__function_NAME_return:` is not a function.
- **Multi-file (`-C`).** The object stores the records (format version 3, see `docs/OBJECT.md`) and `v32asld -g` writes
  `NAME.vbin.vars` as `v32as -g` would for the joined source; `v32asld -R` keeps them. Tested with two files, one function each.
- **`-Wmarkers`** also covers `;@var` (no NAME or no `loc=`), `;@scope` (not `{` or `}`) and the block errors above.
- `--standard`: the `;@` lines are comments; no `.vars`.
- Tests: `tests/markers/` has the `v32lua -g` samples `lua_dbg.asm` and `lua_edge.asm` (the runtime replaced by stubs) with
  their `.debug` and `.vars`, `vars.asm` (every rule above), and `vl1.asm`/`vl2.asm` (linked == joined).

## Release preparation (2026-11-04)

- `make install` also installs `v32asld`, the macro libraries and the man pages; `make uninstall`, `make mancheck` added;
  `make sysinstall` installs under `/usr/local`.
- Manual pages: `v32as(1)`, `v32asld(1)`, `v32as-language(7)`, `v32as-inc(3)`, `v32as-control(3)`.
- `README.md` rewritten as a user-facing document (this file takes over the round-by-round notes).
- Documentation corrected where it disagreed with the program: the `--standard` row for `datafile`, the format of the `-E`
  markers (`# LINE "FILE"`), the `v32asld -w` help text, the message quoted in `lib/v32as.inc`, and the status notes in
  `docs/PREPROCESSOR.md`. `v32as --help` now lists `-B, --binary` and says that `-I` also applies to `datafile`.
