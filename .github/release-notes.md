**v32as** is an alternate, drop-in assembler for [Vircon32](https://www.vircon32.com), written in C, with a linker (`v32asld`) beside it. Give it the same source as the official `assemble` and it writes the same bytes; `v32as game.asm` writes `game.vbin` exactly as `assemble game.asm` does. Everything else is an addition you can use or ignore, and `--standard` switches all of it off.

This is the first public release.

## How it differs from the official assembler

- **A real preprocessor.** The official tool has `%define`, `%ifdef` and `%include`. v32as keeps them with the same meaning and adds function-like defines, `%if` expressions and compile-time arithmetic (`%eval`), multi-line macros that can check their operands, `%rep` loops, and file-local labels. It ships two small macro libraries: helpers like `INC`, `CLAMP`, `SWAP` and `PUSHALL`, and structured `IF`/`ELSE`/`ENDIF` and `WHILE`/`ENDWHILE`.
- **The internal registers.** `MOV` can read and write the CPU's `IP`, `IR` and `IV` registers, which makes self-patching code possible.
- **A friendlier language.** It adds binary, octal and `h`-suffix numbers, character literals, `true`/`false`, `label+N` operands, a label and an instruction on one line, and case-insensitive mnemonics.
- **Separate assembly and linking.** `-C` writes relocatable objects, and `v32asld` links them into a VBIN or packs them into archives.
- **Better errors.** Messages name the file and line you wrote, even inside `%include` files and macros, and show the include and expansion chain.
- **More for tools and debuggers.** It adds listings (`-T`), a verbose or step-by-step trace, and `.debug` files with byte offsets or C-level lines (`--map`, for `v32cc`). It can also write symbol, variable and relocation tables and Makefile dependency files.
- **Cartridge paperwork.** `%title`, `%texture` and `%sound` in the source make v32as write the XML that `packrom` reads, plus `Make.sh`/`Make.bat` build scripts.
- **An interactive mode.** `v32as -i` assembles each line as you type it and shows the encoding.

## Downloads

Pick the zip for your system: `v32as-@VERSION@-<os>-<arch>.zip`.

| System | Architectures |
| --- | --- |
| Windows | x86_64, arm64 |
| macOS | x86_64 (Intel), arm64 (Apple silicon) |
| Linux | x86_64, arm64, armv7 and armv8 (Raspberry Pi) |
| OpenBSD, Haiku | included when their (optional) builds succeeded |

Each zip unpacks straight into your `v32tools` folder (`/usr/local/Vircon32/v32tools`, or `C:\Program Files\Vircon32\v32tools` on Windows), or anywhere on your `PATH`. It holds:

- `v32as` and `v32asld`;
- the macro libraries in `include/v32as/`, which v32as finds by itself;
- the README, the usage guide and the manual pages in `docs/v32as/`.

Check a download against `SHA256SUMS.txt`. To pack cartridges you also need the official Vircon32 DevTools (`packrom`, `png2vircon`, `wav2vircon`).

The Linux, Windows and BSD binaries are built without GNU readline, so the interactive mode works there without line editing or history. Build from source with readline installed to get them.

## Quick start

```sh
v32as game.asm                      # game.vbin, as the official assembler writes it
v32as -i -c                         # try instructions interactively at cartridge addresses
v32as --rom-xml game.xml game.asm   # also the packrom definition, from %title/%texture/%sound
packrom game.xml                    # (official DevTools) game.v32
```

For a guided tour, start with `docs/USAGE.md`. The [README](https://github.com/wedge1020/v32as#readme) is the full reference, and the manual pages are `v32as(1)`, `v32asld(1)`, `v32as-language(7)`, `v32as-inc(3)` and `v32as-control(3)`. Bug reports and feature requests are welcome in the issues.
