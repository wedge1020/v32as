# Relocatable objects and the linker

`v32as -C FILE.asm` writes `FILE.v32ob`, a relocatable object. `v32asld` links objects into a VBIN.
(`-c` is already `--cart`, so the object option is `-C`/`--object`.)

```
    v32as -C a.asm              # -> a.v32ob
    v32as -C b.asm -o b.v32ob
    v32asld -o prog.vbin [-b | -c | -O ADDR] [-g[MODE]] [-r FILE] a.v32ob b.v32ob libx.a
```

Linking is exactly equivalent to assembling the objects' sources one after the other as a single file at the
chosen base (the test suite checks this byte for byte). Objects are placed in command line order and the first
word of the first object is the entry point.

## Source language

```
global _name, _other        the label is defined here and visible to other objects
extern _name                the label is defined in another object
```

Both take plain label names (no `%`), are errors under `--standard`, and may appear anywhere in the file.
A label not named by either is local to its object (two objects may reuse a local name). Rules:

* a `global` label must be defined in the same file; an `extern` one must not be;
* a label cannot be both; `label +/- integer` works on externs (`mov R0, _table+2`, `pointer _t+1`);
* outside `-C`, `global` is accepted (and checked), `extern` references simply stay undefined labels.

In an object, labels are relative to the object (its first word is 0). `-b -c -O -g -r -T -n -E` are not
allowed with `-C` (`--map FILE` is: it replaces the assembly file and line of the debug records by the C ones): the base address is the linker's job, and debug records travel inside the object.

## Object format (version 1; version 2 adds datasyms and function names)

All numbers are 32-bit little endian words.

| field | size | |
|---|---|---|
| magic | 8 bytes | `V32-VOBJ` |
| version | 1 | `1` |
| nwords, nsyms, nrels, ndbg, strsize | 5 | counts, and the string table size in bytes |
| words | nwords | the program: code and data, addresses as assembled at base 0 |
| symbols | 3 x nsyms | `name` (string offset), `value` (word offset, 0 if undefined), `flags` (1 defined, 2 global, 4 extern, 8 label followed by code, 16 datasym: version 2, value 0, defined, never moved) |
| relocations | 3 x nrels | `word` (index), `symbol` (index, or `0xFFFFFFFF`), `addend` |
| debug | 3 x ndbg (4 x in version 2) | `word`, `file` (string offset), `line`: one per instruction; version 2 adds `func`, a string offset or `0xFFFFFFFF`, the function of a `--map` entry |
| string table | strsize bytes | NUL terminated strings |

A version 2 object is written when the source has a `datasym`, `--map` was given or the source has `;@src` markers; version 1 otherwise. `v32asld` reads both.

With `;@src` markers (V32DBG) the debug records are the marker entries, not one per instruction: `word` is the first instruction after the marker, `file`/`line` the marker's, `func` the `__function_NAME` in effect (`NAME`) or `0xFFFFFFFE`, which says "no function" (the linker then writes no label for the record, as `v32as -g` does not). `--map` entries replace the marker's for the instructions the map names.

A relocation says: the final value of `word` is `address(symbol) + addend`. With symbol `0xFFFFFFFF` it is
`load address of the object + addend` (the values computed from an instruction's own address: `MOV Rn, IP`,
the `IR`/`IV` patch addresses). For a datasym the final value is just `addend` (the base of the data is not the program's business), and `v32asld -r` marks the word `D`. `address(symbol)` is the symbol's offset plus the object's final position for a
defined symbol, or the address of the global of that name for an `extern` one.

## `v32asld`

| option | |
|---|---|
| `-o FILE` | output (default `a.vbin`) |
| `-b`, `-c`, `-O ADDR` | base: BIOS, cartridge (default) or any address |
| `-g[MODE]` | writes `FILE.debug` like `v32as -g` (`program`, `vbin`, `v32`); labels are every label of every object, local or global, and the lines are the sources' own |
| `-r FILE`, `--emit-relocs FILE` | `index,0xVALUE[,C\|D]` for each word holding an address, as `v32as -r` (see the README for the format) |
| `-R`, `--relocatable` | partial link: write one object (default `a.v32ob`) joining the inputs. Defined symbols keep their kind and move by their object's start, an `extern` that an input defines becomes a reference to the definition, the rest stay `extern`; relocations, datasyms and debug records are kept. Linking the result equals linking the inputs. Not with `-b -c -O -g -r --symbols --entry --v32x` |
| `--symbols FILE` | `0xADDRESS,name,kind` for every label of every object (kind `F` code, `D` data), sorted by address; local labels appear under the name the assembler gave them (`_f__loop`) |
| `--entry SYMBOL` | the program starts at a global label: unless it is the first word of the first object, a two-word `jmp SYMBOL` is placed in front of everything (it appears in `-r`) |
| `-L DIR`, `-l NAME` | archives `DIR/NAME.v32a`, else `DIR/libNAME.a` (`.` is always searched last) |
| `--v32x --data WORDS` | write a V32X executable instead of a VBIN: 8 words (`0x58323356`, version 1, 8, text words, data words, entry offset, origin, 0) and then the image, byte for byte what `tools/v32x.py` makes from `v32as --origin`. The origin is the base, default `0x100000` (`WINDOW_BASE`; `-O` changes it, `-b`/`-c` and `-g vbin|v32` are refused). `--entry` is then a global label or a word offset into the image (default 0), goes into the header and adds no jump; it must lie inside the image |
| `-A FILE` | make the archive FILE from the objects named, instead of linking |
| `-w` | no warnings (an `extern` nobody uses is warned about) |

Errors: an undefined symbol (``undefined reference to `x' (first used in a.v32ob)``, once per symbol), a global
defined by two objects, a file that is not an object or is truncated or inconsistent.

## Archives (`V32-VARC`)

`v32asld -A libx.a a.v32ob b.v32ob` bundles objects. Operands that are archives, and `-l` ones, are read after
all plain objects; a member is linked in (appended, in the order found) only when it defines a global that some
selected object still lacks, and the scan repeats until nothing more is pulled in.

| field | size | |
|---|---|---|
| magic | 8 bytes | `V32-VARC` |
| version, nmembers | 2 | `1`, count |
| per member | | name length, name (zero padded to 4 bytes), data size, data (the object, zero padded to 4 bytes) |
