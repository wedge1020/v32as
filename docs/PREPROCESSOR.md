# v32as preprocessor: design exploration

Status: **Phases 1 and 2 implemented** (sections 9 and 10); the rest is proposal. Original exploration notes follow; it is based on reading `v32as` (flex/bison, ~3000 lines),
the stock `DevelopmentTools/Assembler/VirconASMPreprocessor.*`, and v32c++ `src/macro.c`.
(No flex in the sandbox this was written in, so nothing was compiled or run.)

---

## 1. What exists today

### v32as
- **No preprocessing stage.** `usage()` says "run the source through v32pp first".
- **Single entry point:** `asm_parse(a, name, text, len, first_line)`, called from 3 places in `main.c`
  (REPL line, stdin, each file). `text` goes straight into one flex buffer.
- **The flex scanner is not preprocessor-friendly.** It classifies identifiers against `tables.c`
  and *interns labels as a side effect of lexing* (`asm_sym_intern`). An unknown identifier is a hard
  lexer error. So macro names must be gone before the scanner ever sees the text.
- **File/line is consumed in four places**, all reading `a->cur_file` + `loc->first_line`:
  `report()` (diagnostics), `fixup_t` (undefined-label errors in `asm_finish`), `dbgrec_t` (`-g` CSV),
  and `open_data_file()` (`datafile` resolves relative to the current file's directory).
- Lexical facts a preprocessor must respect: comments are `;`; numbers include `FFh`, `0b..`, octal,
  `#42` / `#-3` (decimal with a `#` prefix), floats `d+.d+`; char literals `'A'`; strings with escapes;
  labels are `_name:` (must start with `_`).

### Stock `assemble` preprocessor (the parity contract)
Token-based, line-oriented; a directive is a line whose **first token is `%`** (whitespace around it,
even `%   define`, is irrelevant because it works on tokens).

| directive | stock behaviour |
|---|---|
| `%define NAME tokens...` | object-like only; value = rest of line (comment stripped); stored **unexpanded** and expanded lazily at use; silent redefinition; error if value contains `NAME` or `%` |
| `%undef NAME` | silent if undefined |
| `%ifdef` / `%ifndef` / `%else` / `%endif` | one identifier argument (not macro-expanded); nesting OK; error on `%else` twice or with no `%if`; **unterminated `%if` at end of file is not diagnosed** |
| `%include "f"` | string literal only; resolved against **the including file's directory**, no search path, no CWD fallback; depth > 20 is an error; defines are global across includes |
| `%error "m"` / `%warning "m"` | error aborts; warning respects the disable flag |
| inside a false region | only `ifdef/ifndef/else/endif` are processed; every other line, **including unknown directives**, is dropped |
| substitution | only on non-directive lines, only on identifier tokens (never in strings/char literals/comments), re-scanned up to 10 times then "too deep" |
| diagnostics | `file:line: preprocessor error: msg` |

Stock quirks I would *not* copy: `ReplaceDefinitions` inserts into `ProcessedTokens` using an iterator
that belongs to `Line` (undefined behaviour that happens to work), and the `%endif` message says `#endif`.

### v32c++ `macro.c`
A self-contained C-preprocessor engine (~1360 lines): object- and function-like macros, `#`/`##`,
variadics with GNU comma swallowing, hide-set self-reference protection, `#if` expression evaluator,
`__FILE__`/`__LINE__`, redefinition comparison.
- **Reusable nearly as-is:** buffer/splice, hide sets, `collect_args`, `substitute`, stringize/paste, evaluator,
  `same_definition`.
- **C-specific, must change:** `//` and `/* */` comments (assembly uses `;`), `@annotation@` machinery,
  named-constant tracking, C passthrough/stability logic, `NEED_MORE` multi-line invocation joining
  (assembly is line-oriented, so this *simplifies*), and the **global static macro table** (needs to become
  an instance: REPL, multiple inputs, tests).

---

## 2. Recommended architecture: a text-to-text stage with a line map

```
 source text ──► pp (new) ──► expanded text + linemap ──► asm_parse (flex/bison, unchanged) ──► asm_t
                  │
                  └─ -E: print expanded text instead of assembling
```

**Why text level and not a token filter between flex and bison** (what stock does):
1. The scanner interns labels and rejects unknown identifiers *while lexing*; a token filter would need
   a new `T_IDENT` path through the lexer and grammar.
2. `##` pasting, `%rep`, multi-line macros and generated labels all produce new source text; re-lexing
   text is trivial, re-classifying pasted tokens is not.
3. The REPL, `-E`, and "macro generates a directive line" all fall out for free.

The price: the pp needs its own small scanner that mirrors `lexer.l`'s lexical rules (comments, strings,
char literals, the number forms above), so it never substitutes inside them.

**Line map instead of `#line` markers.** The pp returns, for every output line, `{file_id, line, origin}`.
`asm_parse` takes the map; `lex_line` stays the *output* line; one function `asm_resolve(a, line)` →
`(file, line)` replaces the four `cur_file` uses above. `origin` carries "expanded from macro X defined at
f:l" for diagnostics. File names are interned for the life of the run (fixups and `-g` records hold
`const char *`). Markers (`# 12 "f.asm"`) are still printed by `-E` for humans/tools. `datafile` keeps
resolving relative to the *included* file because it goes through `asm_resolve`.

**Module split** (new files, `v32as.h` gets ~40 lines of types):

| file | job | origin |
|---|---|---|
| `pp_scan.c` | asm-aware scanner: ident / number / string / char / `;` comment / punct | new, mirrors `lexer.l` |
| `pp_macro.c` | table (per-instance), object+function-like, args, `#`/`##`, variadics, hide sets | adapted from `macro.c` |
| `pp_expr.c` | `%if` / `%assign` evaluator | adapted from `macro.c` (`ev_*`) |
| `pp_dir.c` | directive dispatch, conditional stack, include stack, `%rep`/`%macro` capture | new |
| `pp_out.c` | output buffer + linemap + interning + `-E` printer | new |

API sketch:
```c
pp_t *pp_new   (const pp_opts_t *o);                    // -I dirs, base mode, warning flags
int   pp_define(pp_t *p, const char *spec);             // "NAME" or "NAME=value"  (-D)
int   pp_run   (pp_t *p, const char *name, const char *text, size_t len); // appends to output
int   pp_pending(const pp_t *p);                        // inside %macro/%rep/%if? (REPL continuation)
const pp_result_t *pp_result(pp_t *p);                  // text, len, linemap
```

---

## 3. Features, in tiers

### Tier 0: parity (must ship first; stock sources assemble identically)
Everything in the table in section 1, same semantics: lazy object-like `%define`, includes relative to the
including file, depth limit 20, `%ifdef` argument not expanded, false regions skip unknown directives.
Allowed supersets: directive names case-insensitive (mnemonics already are); `%define` bodies may contain
`%` and self-reference (hide sets make that safe).
**Deliberate divergences** (listed so they are decisions, not accidents): unterminated `%if` becomes an
error; self-referential `%define` becomes legal; the 10-cycle rescan limit is replaced by hide sets plus
an expansion-depth guard.

### Tier 1: the "strong cpp" layer (low risk, high value)
- **Function-like `%define`**: `%define LO(x) ((x) & 0xFFFF)`, variadics `...`, `#x` stringize, `a ## b` paste.
  `#` is safe even though `#42` is a literal: stringize applies only when `#` is immediately followed by a
  *parameter name*, and a parameter can't start with a digit.
- **`%if expr` / `%elif` / `%elifdef` / `%elifndef`**, with `defined(X)`, integer arithmetic, comparisons,
  `&& || ! ?:`, all of v32as's number forms. (Evaluator reused.)
- **`%assign N expr`** (evaluate now, unlike lazy `%define`).
- **Predefined symbols**: `__FILE__`, `__LINE__`, `__V32AS__` (version), `__BIOS__` / `__CART__` / `__NONE__`
  (from `-b/-c/-n`, so one source can build for either), `__ORIGIN__`, `__INCLUDE_LEVEL__`, `__COUNTER__`.
  (No `__DATE__`/`__TIME__` by default: reproducible builds.)
- **CLI**: `-D NAME[=val]`, `-U NAME`, `-I dir` (repeatable), `%include <name>` searches `-I` dirs,
  `%include "name"` tries the including file's directory first, then `-I`. `-E` (expanded source to stdout/`-o`),
  `-P` (omit line markers), `-MD/-MF` (Makefile dependency list).
- **`%includeonce`** (or `%pragma once`), `%line n "file"`.
- **File-local labels**: `.loop:` / `jt R1, .loop` become `<last global label>__loop`. Unambiguous with
  floats because v32as floats need digits on both sides of the dot. Generated names stay valid
  `_[A-Za-z0-9_]+` labels.

### Tier 2: assembly-native macros (where most of the value is)
```asm
%macro CLAMP reg, lo, hi              ; named params; %1 %2 %3 and %0 (count) also work
    imax  reg, lo
    imin  reg, hi
%endmacro

%macro WAIT_FRAMES n=1                ; defaults
  %rep n
    wait
  %endrep
%endmacro

%macro COUNTDOWN reg, count
    mov   reg, count
%%top:                                ; macro-local label -> __M<serial>_top
    ; ...body...
    isub  reg, 1
    jt    reg, %%top
%endmacro
```
- `%macro NAME params` / `%endmacro`, `%exitmacro`, defaults, variadic tail, `%0`, `%rotate`, `%%local` labels
  (serial per expansion), invoked as a statement (`CLAMP R1, 0, 255`), after labels on the same line.
- `%rep expr` / `%endrep` / `%exitrep`; optional `%for i, a..b`.
- **Operand-type predicates, which only v32as can offer because `tables.c` already knows the ISA:**
  `%ifreg x`, `%ifnum x`, `%ifstr x`, `%iflabel x`, `%ifport x`, `%ifidn a, b` / `%ifidni`.
  This enables overloaded macros (`INC x` picks `iadd reg, 1` or `mov`-via-memory forms) and
  compile-time checking with real messages (`%error "CLAMP wants a register"`).
- Macro name wins over a mnemonic of the same name (as NASM; `%define MOV` already works that way in
  stock). A warning when it shadows a real opcode, silenced with an explicit `%pragma shadow`.

### Tier 3: stretch
`%idefine` / `%imacro` (case-insensitive names), string helpers (`%strlen`, `%substr`, `%strcat`, `%defstr`),
context stack (`%push`/`%pop`/`%$local`) for structured `IF/ELSE/ENDIF` and `WHILE` macro libraries,
`%unmacro`, float-aware `%assign`.

### Library layer (not part of the engine)
A shipped `v32as.inc` made of Tier 2 macros: multi-register `PUSH`/`POP`, frame enter/leave, `INC/DEC/NEG/ZERO`,
`SWAP`, and wrappers over the `IP`/`IR`/`IV` expansions (e.g. "patch next instruction's immediate").
Those wrappers are where the preprocessor and the existing register aliases combine; the "must run from
RAM" warning in asm.c already covers the dangerous cases.

---

## 4. Integration (file by file)

| file | change |
|---|---|
| `main.c` | new options (`-D -U -I -E -P -M*`); read text → `pp_run` → `asm_parse` (all 3 call sites); REPL keeps one `pp_t` alive and uses `pp_pending()` for continuation prompts; update the `usage()` note |
| `asm.c` | `asm_parse` accepts the linemap; add `asm_resolve`; replace `cur_file` reads at report/fixup/dbgrec/`open_data_file`; step mode reports the original `file:line`, not the expanded line |
| `v32as.h` | `pp_*` types, `asm_resolve`, interned file table |
| `lexer.l`, `parser.y` | **no change** |
| `Makefile` | add `pp_*.o` |
| `tests/` | see below |

**Multiple input files** (`v32as a.asm b.asm`): suggest one `pp_t` for the whole run (as if concatenated),
since they already share one symbol table.

**Diagnostics**: keep v32as's `file:line:col: error:` format; add
`note: in expansion of macro 'X' (defined at f:l)` and `In file included from f:l` chains. Limits: include depth 20
(parity), macro expansion depth, `%rep` iteration cap, output size cap.

## 5. Test plan
1. **Parity (differential):** the stock `Tests/` files (`Ifdef`, `IncludeMain`/`IncludeAux`, `ErrorDirective`,
   `TestLexer`, `FullExample`) plus new ones exercising each row of the section 1 table, including false-region
   unknown directives and `%   define` spacing. Compare VBIN hex **and** `.debug` against the official
   `assemble`. Verify the include path text in `.debug` specifically: stock builds it as
   `<dir-with-trailing-sep> + sep + name` and then `NormalizePath`s it, so `./` / `//` handling must match.
2. **Golden tests** for every new feature, using the existing `run_tests.sh` layout
   (`cases/`, `golden/`, `errors/*.expect`), plus `-E` output goldens for macro expansion.
3. **Negative tests:** runaway recursion, unbalanced `%if`/`%endmacro`/`%endrep`, arg-count mismatch,
   include cycle, macro generating a bad label.
4. **Fuzz/regression:** no-directive inputs must be byte-identical before and after (the pp is a no-op
   for files that contain neither `%` lines nor defined names).

## 6. Sharp edges to remember
- `#` is both a literal prefix and (in macro bodies) stringize; rule above keeps them apart.
- A bare hex-looking word like `ABh` is a *number* to `lexer.l`; the pp scanner must use the same
  longest-match so such tokens are never treated as macro names by accident.
- Labels must start with `_`, so every generated label (`%%x`, `.x`) is built to match `_[A-Za-z0-9_]+`.
- `-g` attributes every instruction from a macro expansion to the **call site** by default (keeps the CSV format
  unchanged); an opt-in extra column for macro origin is possible later.
- Case: macro names case-sensitive (stock), mnemonics/registers case-insensitive (v32as).

## 7. Phasing
1. **Infrastructure + parity** (pp core, scanner, linemap/`asm_resolve`, Tier 0, `-E`, parity tests).
2. **Tier 1** (function-like `%define`, `%if`, `%assign`, predefined, `-D/-I`, `.local` labels).
3. **Tier 2** (`%macro`, `%rep`, predicates).
4. Tier 3 / library as wanted.

Each phase leaves a shippable assembler that is a strict superset of the previous one.

## 8. Decisions (settled)
1. Text-level pre-pass, provided it is resilient. **Yes.**
2. Multiple input files: **one shared preprocessor state** (matches GNU `as`, and the shared symbol table).
   C compilers instead use one translation unit per file; that would be a one-line switch if ever wanted.
3. Macro parameters: **named** (positional `%1` as alias).
4. Directive names: **case-insensitive** (macro names stay case-sensitive, as in stock).
5. `%assign`/`%if`/`%eval` arithmetic: **integers and floats**.
6. No external preprocessor is planned, so `-E` is only a debugging aid (kept because it is nearly free).
7. **`%eval`** (new requirement): in assembly `#define MATH A+B` cannot work, because the sum must exist as a
   *value* in the source. `%eval` evaluates an expression over symbols/macros with basic math and yields the
   result in its place. Plan: both a directive form (`%eval NAME expr`, like `%assign`) and an inline form
   usable inside operands (`mov R0, %eval(WIDTH*2+1)`). Int and float aware: `%eval(1/2)` is integer
   division while `%eval(1.0/2)` is `0.5`. Limits: 32-bit wrap on integers (what the CPU holds), float32
   rounding of float results, clear errors on divide by zero or overflow. Phase 2.

## 9. Phase 1 status (implemented)
Files: `pp.h`, `pp.c` (preprocessor), `asm.c` (`asm_resolve`, `asm_parse_map`), `main.c` (`-D -U -E -P`, REPL).
Parity directives, line map, include chain notes in diagnostics, `-g` file/line through includes,
`datafile` relative to the included file. Tests: `tests/pp/` (source vs hand-expanded `.ref`, `-E` goldens,
`.debug` golden), `tests/errors/pp_*`. 48/48 pass, including under ASan/UBSan.
Known divergence to remember for Phase 2: stock reads `%define F(x) ...` as object-like macro `F` with body
`(x) ...`; Phase 2 function-like macros will follow C (`NAME(` with no space) and this will change.

## 10. Phase 2 status (implemented)
Added: function-like `%define` (`#`, `##`, variadics, GNU comma swallowing), splice-based expansion with hide
regions (so `%define F G` then `F(x)` works), `%if`/`%elif`/`%elifdef`/`%elifndef`, `%assign` (and `%eval NAME expr`),
inline `%eval(expr)`, integer+float expression engine (`ppexpr.c`), `__FILE__ __LINE__ __COUNTER__
__INCLUDE_LEVEL__ __V32AS__ __BASE__ __BIOS__/__CART__/__ORIGIN__/__NONE__`, `-I`, `%include <...>`, `%includeonce`,
`.local` labels, `-w`.
Command line: **binary is now the default output** (cart base, `input.vbin`, as the official tool); `-T/--text`
gives the old listing; `--standard` switches every extension off (table in README).
`--standard` found one real behavioural difference worth knowing: the official lexer reads `0777` as decimal, v32as
(extended mode) as octal; extended mode now warns on each such literal.
Decisions taken during Phase 2: `%assign`/`%eval NAME` are the same directive; `%eval(...)` must be followed by `(`
(a line *starting* with `%eval(` is source text, not a directive); float results print as the shortest `d+.d+`
that round-trips (the scanner has no exponent form); `.local` labels expand to `<global>__<name>`.
(Phase 3 below completes that list.)

## 11. Phase 3 status (implemented)
Added: `%macro`/`%imacro`/`%endmacro`, `%unmacro`, `%rep`/`%endrep`/`%exitrep`, `%exitmacro`, `%rotate`, `%%local`
labels and `%@` (rep iteration), operand tests (`%ifreg %ifsysreg %ifnum %ifstr %iflabel %ifport %ifidn %ifidni
%ifempty %ifmacro %ifctx`, each with `n` negation and `%elif` forms), `%idefine`/`%xdefine`/`%ixdefine`, `%strlen
%substr %strcat %defstr`, the `%push`/`%pop`/`%repl` context stack with `%$name`, `%pragma once|shadow`, the macro
libraries `lib/v32as.inc` and `lib/control.inc`, REPL continuation (`pp_pending`).

How it works (pp.c): `%macro`/`%rep` switch the line processor into *capture* mode: raw lines are stored (conditionals
are not evaluated) until the matching terminator, counting nested openers. Running a body feeds each stored line,
after argument substitution, back through `process_line`, so a body may hold directives, conditionals, further
macros and loops. Execution contexts (`ctx_t`: macro or rep) form a stack that carries the arguments, `%rotate`
state, the `%%` serial, and the `%if` depth at entry (an `%else`/`%endif` cannot close a conditional opened outside
the macro; one left open inside is an error). `%exitmacro`/`%exitrep` set an unwind request that the replay loops
honour. A macro is invoked from `process_line` when the first token after optional `label:` prefixes names one.
Lines inside a *nested* `%rep` stay raw until that loop runs, so `%rotate` and `%@` take effect per iteration.
Source positions: everything a body produces is attributed to the invocation line (flag `expanded` set), which keeps
the `-g` format unchanged.

Decisions taken: statement macros live in their own namespace (not the `%define` one) and are matched on the first
token only; overloads by argument count; an argument that is empty takes the default; `%N` of an optional argument
that was left out is empty, beyond the declared maximum it is an error; in lines that will not be looked at (dead
branch, `%elif` after a taken branch) a missing `%N` is not an error; `%%x` is per expansion, per iteration only for a
`%rep` outside any macro; a macro hides a built-in name with a warning unless `%pragma shadow`; contexts are global to
the run (not per file) and an unterminated `%push` at the end of the outermost file is an error.
Limits: 64 nested expansions, 1,000,000 iterations per `%rep`, about 4.2 million replayed body lines per run
(`MAX_WORK_LINES`), a `%rep` stops at its first iteration that reports an error.

Not done: `%for`, `%local` (context-local `%define` is possible through `%$name`), `%rep` over a list, `-MD/-MF`
dependency output, wrappers over the `IP`/`IR`/`IV` expansions (waiting for the result of `experiments/sysreg` on a
real console/emulator before putting self-modifying code into a library).

Tests: `tests/pp/macro_*.asm`, `rep_basic`, `ctx_control`, `lib_macros`, `str_directives`, `idefine_imacro`
(assembled output equals the hand-expanded `.ref`), `pp/expand/macros` (`-E` golden), `pp/dbg_macro` (`.debug`
golden: generated code is attributed to the invocation), `errors/pp3_*` (messages) and `std/*_directive` (all of it
is off under `--standard`). ASan/UBSan clean, plus a few thousand mutated sources without a crash or leak.
