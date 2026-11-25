# Changes

The development history of `v32as` and `v32asld`, by round, newest first. The current behaviour of everything
listed here is described in `README.md`; this file records what each round (a set of requests from the
projects that use the assembler) added or fixed. Before round 9 the assembler had the features described in the
README's "History and compatibility" section and the preprocessor phases 1 to 3 of `docs/PREPROCESSOR.md`.

## 2026-11-25: first release (20261125-release)

- **`-i, --interactive`** starts the interactive mode. Before, it started whenever there was no input file and STDIN was a
  terminal; now that case prints the usage summary (STDERR, status 2). A pipe or redirection on STDIN is still assembled to
  STDOUT, and `-` still reads STDIN. `-i` takes no input files and refuses the options that write files or need a whole
  program (`-o -E -B -C -g -r --symbols --map -MD --rom-xml --rom-xml-opt --build-scripts --v32x -s`).
- **`--help` fits an 80-column terminal** (v32as and v32asld), grouped by topic, as a series of `fputs`.
- **Fixes (crashes and memory safety)**: an output file that cannot be written crashed in `fclose (NULL)` (now status 2); a
  macro that `%unmacro`s itself while running was used after being freed; a deeply nested `%if`/`%eval`/`%assign` expression
  overflowed the stack (now "expression is nested too deeply"); on 32-bit hosts (armv7) a corrupt object's word counts could
  wrap the linker's size check and read out of bounds.
- **Fixes (behaviour)**: a recursive macro stops at its first "nested too deeply" error instead of one per leaf of the
  expansion tree (millions of lines); long single-line expansions no longer take quadratic time (`splice` drops spent hide
  regions); a directory as the input file, `%include` file or `datafile` is an error instead of "out of memory" or empty
  input; `-o` naming an input file is refused (it used to truncate the source); a listing or `-E` output that stopped at an
  error is removed, so `make` does not take it as up to date; `--v32x-data` checks its number; `v32asld -A` reads every
  member before it writes, so a failed run no longer destroys an existing archive; errors inside a `%rep` outside any macro
  point at the body line, not at `%endrep`.
- **`--standard` closer to the official tool**: a bare port value only as the value of `out`; a decimal literal above
  2147483647 and a denormal float are errors; `-0xFFFFFFFF` wraps; a trailing comma in `integer`/`float` lists is accepted;
  directive keywords are lowercase only. Accepted cases that stay are listed in the README.
- **Portability**: Windows (MinGW-w64) builds: `inc/portable.h` (`_fullpath` for `realpath`, `gnu_printf` format checks,
  binary-mode stdio so a VBIN on STDOUT and the text side files are the same bytes as elsewhere), no `getline` in the
  non-readline REPL, `\` as a path separator for output names and build scripts; the heapstat stubs are `static inline`; the
  Makefile passes `CFLAGS`/`LDFLAGS` when linking (sanitizer, `-m32` and cross builds); an empty `LIBDIR` (relocatable
  builds) is skipped.
- **Release plumbing**: `.github/workflows/release-builds.yml` (Windows x86_64/arm64, macOS, Linux x86_64/arm64/armv7/armv8,
  optional OpenBSD and Haiku; zips in the `v32tools` layout, checksums, a draft release from `.github/release-notes.md`),
  `.github/workflows/tests.yml` (the suite on every push: gcc, clang without readline, ASan/UBSan; macOS build), and
  `scripts/smoke_test.sh` (versions, `%include <...>` from the package layout, `-C` + link, `-i`). `.gitignore` no longer
  hides the tests' golden `.hex`, `.map` and `.out` files.
- **Tests run on macOS/BSD**: `tests/run_tests.sh` no longer needs GNU `od -w` (a `hex4` helper over plain `od`, in the
  goldens' format) or GNU `wc` spacing; `tests.yml` runs the suite on macOS too.
- **Documentation**: `docs/USAGE.md` (a task-oriented guide), README quick start and "How it differs from the official
  assembler", the library search path and the `--standard` details documented, man pages updated for `-i`.

## 2026-11-21: memory use, round 2 (VA-7, VA-8, VA-11 of the v32nix proposal), `--stats`

- **File-local label names are kept only when read.** The preprocessor noted every `.local` label twice (mangled and `function.local`) in a
  table; now only for `-g` and `--symbols` (`pp_set_keep_locals`), as the mangled name and the length of its global part; `function.local` is
  made when `--symbols-all` asks for it (`pp_local_name`), `-g` and `--symbols` without `-all` ask only whether a label is local (`pp_is_local`).
  Nothing else reads them (errors, `-C`, `-r`, `--map` and the REPL use the assembler's own names).
- **Arrays sized from the input.** The line map is reserved from the line count (a text) or the file size (a file) and cut to its lines after
  preprocessing, and freed once the text is assembled; the output words and the forward references are reserved from the length of the text,
  and `asm_finish` frees the fixups and cuts words, address list and debug records to size.
- **`--stats`**: the high-water mark and live heap of v32as itself on STDERR (`inc/heapstat.h`, `src/heapstat.c`: counting wrappers over
  malloc/calloc/realloc/free/strdup, sizes from `malloc_usable_size`/`malloc_size`; not available elsewhere).
- Outputs are byte-identical, with and without `-g`, `--symbols`, `--symbols-all`. Tests: `tests/locals` (goldens made with the previous build).
- **VA-9, streaming**: for binary output the last (usually only) input file is assembled as the preprocessor emits it
  (`pp_set_sink`; `asm_stream_begin/line/end`), one line per `yyparse` on a reusable buffer (`v32_lex_line_begin/end`), so neither the
  preprocessed text nor the line map is kept; the line being parsed is located by `asm_resolve` from the line's own `srcloc_t`. Forward
  references keep file, line and column only (`fixup_t`, two words less). The assembler's messages for that file are held back and written when
  the preprocessor is done (dropped if it failed), so order, text and exit status are as when the text was assembled afterwards; `-E`,
  `-T`/`-n`, `-v`, `-s` and files before the last (a failed one must leave the assembler as it was) keep the whole-text path.
  `--stats` "after preprocessing" is then after assembling too. Peak on the stand-in for `ls2` (8.6 K lines): 810 K -> 509 K bytes plain
  (the console saves the text's characters and 3 words a line, about 199 K words), 1272 K -> 970 K with `-g --symbols`. Outputs are
  byte-identical (all tests, 3850 runs of many modes against the previous build, the stand-in and one three times its size).
- **Fix (heapstat link)**: with `V32AS_HEAPSTAT` 0, `heapstat_get` / `heapstat_mark` are static stubs in `inc/heapstat.h`, so a build
  without `src/heapstat.c` (v32nix builds a fixed list of sources) links; `__linux__` counts only with `__GNUC__` and without `__V32CC__`;
  `V32AS_NO_HEAPSTAT` still forces it off.
- **`--stats` on the console** (`__V32CC__`, `inc/heapstat.h`, no new source file): words, from libc's `v32_heap_info` (used, free, largest hole, heap start/end)
  after preprocessing (after assembling, for a streamed file) and at the end, and the **peak break** (`sbrk (0)` minus the heap's start, sampled after every
  `xrealloc`: a lower bound, as libc keeps no high-water mark; stdio and lexer buffers between two samples are not seen). `peak used` is only the larger of the two
  `used` figures. The host path is unchanged. Measured on v32nix in v32sim (the current `v32as.x`, 147 906 words, on the host's `ls2.s`: peak break **91 331** words, the result byte-identical to the host-built `ls2.x`, and the same
  from `cc -S` run on the system); with streaming off, 301 756. The host trace replayed through libc's allocator with word sizes gave 91 812 and 302 239 (original, VA-7/8/9 all off: ~399 K).
- Not done: VA-10 skipped.

## 2026-11-09: ROM definition directives, `--rom-xml`, `--build-scripts`

- **`%title`, `%version`, `%texture`, `%sound`** (not under `--standard`): the cartridge's name and version and its images and sounds, as
  `v32cc` has them in `#title`, `#version`, `#texture`, `#sound`. `%texture NAME "file"` defines NAME as the number of the entry in its list
  (`%version` must be `digits.digits`, as `packrom` requires: an error otherwise; textures and sounds are numbered on their own, in order of first appearance; the same file again keeps its number). The file is looked for
  next to the source holding the directive, then in the `-I` directories. Collected by the preprocessor (`pp_rom_*` in `inc/pp.h`); the assembled
  program is the same with or without them.
- **`--rom-xml FILE`, `--rom-xml-opt FILE`**: write the definition `packrom` reads. A `.png` / `.wav` is named as the `.vtex` / `.vsnd` that
  `png2vircon` / `wav2vircon` make next to the XML (equal base names get `_2`, `_3`, also across upper and lower case); other files by their path
  from the XML. Attribute text is XML-escaped (v32cc's writer does not).
- **`--build-scripts[=sh,bat]`, `--no-build-scripts`, `--build-script-dir DIR`**: `Make.sh` / `Make.bat` that rebuild the cartridge from the
  optimizer on: `v32as -E -P` then `v32opt -O3` on the preprocessed source (only if installed; `v32opt` does not know `%macro`/`%if`/`%rep` and
  deleted a store that an `%else` branch overwrote; a failure only drops the `NAMEOpt` cartridge; `V32OPT_FLAGS` replaces `-O3`), `v32as` (same options, definitions rewritten each
  time), `png2vircon` / `wav2vircon`, `packrom`. Modelled on `v32cc`'s build scripts (`src/buildscript.c`, `inc/buildscript.h`), without the Makefile
  format. The scripts `cd` to their own directory, take the tools from `V32AS`, `V32OPT`, `PACKROM`, `PNG2VIRCON`, `WAV2VIRCON`, and skip the
  optimizer when `NO_OPT` is set.
- **A script is never replaced while it runs, nor a hand-written one at all.** The marker line `Generated by v32as` has to open a comment line; a file
  without it (including `v32cc`'s) is kept with a warning. The scripts call `v32as --no-build-scripts` and set `V32AS_BUILD_SCRIPT`, which
  v32as honours the same way. An unchanged script is not touched; a changed one is written to a temporary file and renamed over the old one.
- `-C` with these directives warns that objects do not store them. Not done: a Makefile format; ROM data in objects and in `v32asld`.
- Tests: `tests/rom_tests.sh` (in `make test`; golden `Make.sh`, `Make.bat` and XML in `tests/rom/golden`, stand-in tools in `tests/rom/stubs`).

## 2026-11-08: CMake build, `-j` fix, `PREFIX` in `sysinstall`; version `20261108-dev`

- **A CMake build** (`CMakeLists.txt`, `cmake/cmake_uninstall.cmake.in`), laid out after v32lua's, alongside the
  Makefile (the two never share products; build in `build-cmake`, not `build`). Needs flex and bison; GNU readline
  is optional (`-DV32AS_READLINE=OFF`). Installs as `make sysinstall` does (`bin/`, `share/man/man{1,3,7}`,
  `share/v32as/lib`), documents under `Vircon32/v32tools/docs/v32as`; on Windows (MSYS2/MinGW) under
  `<Program Files>/Vircon32/v32tools`. `ctest` runs the regression tests, `exe_relative` and `mancheck`.
- **`make -j` no longer corrupts the parser.** `build/parser.tab.c` and `parser.tab.h` were one two-target rule, so
  bison ran twice at once; the header now depends on the `.c`.
- **`make sysinstall` honours `PREFIX`** (it was fixed to `/usr/local`); new `make sysuninstall`.
- `%include` is also looked for in `include/v32as` beside the program (the Windows install layout, moved).
- Version `20261108-dev`.

## 2026-11-05: `OUTPUT.sym` (SA-3), `make version`

- **`%include` finds the macro libraries by itself.** After the `-I` directories (and not with `--standard`), v32as searches the directory `make install` put `lib/*.inc` in (compiled in from `LIBDIR`) and, next to the program, `lib`, `../lib`, `../share/v32as/lib` and `../Vircon32/v32tools/include/v32as`; only those that exist. So `%include "v32as.inc"` works with no `-I` after an install under any prefix or from an unpacked release. The program's own location comes from `/proc/self/exe` (Linux), `GetModuleFileNameA` (Windows) or `_NSGetExecutablePath` (macOS), else `argv[0]`/`$PATH`. The Windows and macOS branches were not compiled here. `tests/exe_relative.sh`.
- **Version 20261105-dev**, stamped with the new `make version [V=... VDATE=...]` (the `VERSION` macros of `v32as` and `v32asld` and the `.TH` line of every man page). `make install` and `make sysinstall` (PREFIX=/usr/local) were already there, with `uninstall`.
- **A resolved symbol file from `-g` (SA-3).** `v32as -g` (not `--standard`) and `v32asld -g` also write `OUTPUT.sym` (`prog.vbin.sym`) in the
  format of v32cc's `NAME.asm.sym`: `kind,name,address,words,cfile,cline` with F (function: top-level label followed by code, name without
  `__function_`, size to the next top-level label, position of its first instruction), R (data label) and V (`%define global_NAME` with words 0
  = not known, `%define var_NAME` of v32lua with words 1). Addresses are always CPU addresses, whatever the `-g` mode. The linker writes F and R
  only (an object holds no `%define`s) and takes a code label for a function when a debug record names it. Details: README, "The .sym file".
  Nothing existing changes except that a new file appears next to `.debug`.
- **`;@home FROM TO` (v32opt's SO-1).** A variable whose home moved (`;@home BP-3 R5` ... `;@home R5 BP-3`, or `TO` = `optimized-out`) gets a new `.vars` record from that point, in its own block; the old one ends there. `tests/markers/c_home.asm`.
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
