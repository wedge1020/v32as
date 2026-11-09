#!/bin/sh
# usage: rom_tests.sh path/to/v32as   (sh, grep, cmp, ls, sed and sleep only)
# %title / %version / %texture / %sound, --rom-xml, and the Make.sh / Make.bat that --build-scripts writes
BIN=${1:-../bin/v32as}
cd "$(dirname "$0")" || exit 1
case $BIN in /*) ;; *) BIN=$OLDPWD/$BIN ;; esac
HERE=$(pwd)
T=${TMPDIR:-/tmp}/v32as_rom_test.$$; mkdir -p "$T"; trap 'rm -rf "$T"' EXIT
pass=0; fail=0
ok()  { pass=$((pass+1)); }
bad() { fail=$((fail+1)); echo "FAIL: $1"; }
inode() { ls -i "$1" | sed 's/ .*//'; }
chmod +x "$HERE"/rom/stubs/* 2>/dev/null     # (an archive may have lost the mode)
PATH=$HERE/rom/stubs:$PATH; export PATH
STUBLOG=$T/stub.log; export STUBLOG
unset V32AS_BUILD_SCRIPT NO_OPT OPTFAIL
V32AS=$BIN; export V32AS                # the scripts call v32as by name: this is the one under test

fresh() { cd "$HERE" || exit 1; rm -rf "$T/p"; cp -R "$HERE/rom/proj" "$T/p"; cd "$T/p" || exit 1; : > "$STUBLOG"; }

# --- the directives: numbers, case, and nothing under --standard
fresh
"$BIN" -E -P game.asm 2>/dev/null | grep -q 'mov R0, 1$' && ok || bad "%texture NAME is the texture's number (bg.png = 1)"
"$BIN" -E -P game.asm 2>/dev/null | grep -q 'mov R1, 0$' && ok || bad "%sound NAME numbers sounds on their own (boom.wav = 0)"
"$BIN" -E -P game.asm 2>/dev/null | grep -q 'mov R2, 1$' && ok || bad "the same file named again keeps its number"
"$BIN" --standard -E game.asm >/dev/null 2>"$T/err" && bad "%title under --standard" || { grep -q 'unsupported preprocessor directive "title"' "$T/err" && ok || bad "--standard message"; }
printf '%%TITLE "x"\n%%Texture T "gfx/bg.png"\n  mov R0, T\n' > up.asm
"$BIN" -E -P up.asm 2>/dev/null | grep -q 'mov R0, 0$' && ok || bad "directive names are case-insensitive"
printf '%%if 0\n%%texture T "gfx/bg.png"\n%%endif\n%%texture U "gfx/font.png"\n  mov R0, U\n' > skip.asm
"$BIN" -E -P skip.asm 2>/dev/null | grep -q 'mov R0, 0$' && ok || bad "a skipped %texture is not counted"

# --- diagnostics
for t in '%title|the title is missing' '%title ""|the title is empty' '%version|the version is missing' \
         '%texture|the file name is missing' '%texture ""|is empty' '%sound X Y|expected a string' '%title "a" junk|expected end of line'; do
  printf '%s\n  hlt\n' "${t%%|*}" > d.asm
  "$BIN" -E -P d.asm >/dev/null 2>"$T/err" && bad "${t%%|*} accepted" || { grep -q "${t#*|}" "$T/err" && ok || bad "message for ${t%%|*}"; }
done
printf '%%texture "nothere.png"\n  hlt\n' > d.asm
"$BIN" -E -P d.asm >/dev/null 2>"$T/err" && grep -q 'cannot find the texture file "nothere.png"' "$T/err" && ok || bad "a missing texture file warns (and assembles)"
for v in 1.2.3 1 1. .5 1.x v1.0; do
  printf '%%version %s\n  hlt\n' "$v" > d.asm
  "$BIN" -E -P d.asm >/dev/null 2>"$T/err" && bad "version $v accepted" || { grep -q 'not of the form MAJOR.MINOR' "$T/err" && ok || bad "message for version $v"; }
done
for v in 1.0 12.345 '"3.1"'; do printf '%%version %s\n  hlt\n' "$v" > d.asm; "$BIN" -E -P d.asm >/dev/null 2>&1 && ok || bad "version $v rejected"; done
printf '%%title "A"\n%%title "B"\n  hlt\n' > d.asm
"$BIN" -E -P d.asm >/dev/null 2>"$T/err" && grep -q '%title changes the title from "A"' "$T/err" && ok || bad "a second %title warns, the last wins"
L=$(printf 'abcdefghij%.0s' 1 2 3 4 5 6 7)
printf '%%title "%s"\n  hlt\n' "$L" > d.asm
"$BIN" --rom-xml t.xml -w d.asm 2>/dev/null; grep -q "title=\"$(printf %s "$L" | cut -c1-63)\"" t.xml && ok || bad "a long title is cut to 63"

# --- the definition
fresh
"$BIN" --rom-xml game.xml game.asm 2>/dev/null && cmp -s game.xml "$HERE/rom/golden/game.xml" && ok || bad "--rom-xml (golden game.xml)"
[ -f gameOpt.xml ] && bad "--rom-xml alone also wrote the Opt twin" || ok
"$BIN" --rom-xml-opt o.xml game.asm 2>/dev/null && cmp -s o.xml "$HERE/rom/golden/gameOpt.xml" && ok || bad "--rom-xml-opt (golden gameOpt.xml)"
"$BIN" --rom-xml sub.xml -o out.vbin game.asm 2>/dev/null; grep -q '<binary path="out.vbin" />' sub.xml && ok || bad "binary named from the XML"
mkdir -p x/y; "$BIN" --rom-xml x/y/g.xml game.asm 2>/dev/null; grep -q '<binary path="../../game.vbin" />' x/y/g.xml && grep -q 'path="../../gfx/pre.vtex"' x/y/g.xml && ok || bad "paths are relative to the XML's directory"
printf '_a: hlt\n' > plain.asm; "$BIN" -b --rom-xml b.xml plain.asm 2>/dev/null
grep -q 'type="bios" title="plain" version="1.0"' b.xml && ok || bad "BIOS definition, title from the file name, version 1.0"
printf '%%texture "a/x.png"\n%%texture "b/x.png"\n%%texture "c/X.PNG"\n _a: hlt\n' > clash.asm
mkdir -p a b c; : > a/x.png; : > b/x.png; : > c/X.PNG
"$BIN" --rom-xml c.xml clash.asm 2>/dev/null; grep -q 'path="x.vtex"' c.xml && grep -q 'path="x_2.vtex"' c.xml && grep -q 'path="X_3.vtex"' c.xml && ok || bad "equal base names (also in another case) get _2, _3"

# --- the options that cannot work
for o in "--rom-xml x.xml --standard" "--build-scripts --standard" "--rom-xml x.xml -T" "--rom-xml x.xml -O 0x100" "--rom-xml x.xml -C" \
         "--build-scripts=zsh" ; do
  "$BIN" $o game.asm >/dev/null 2>&1 && bad "$o accepted" || ok
done
"$BIN" --build-scripts -o x.vbin < game.asm >/dev/null 2>&1 && bad "--build-scripts from STDIN" || ok
"$BIN" -C -o g.v32ob game.asm 2>&1 | grep -q 'not stored in an object' && ok || bad "-C with ROM directives warns"
fresh; ls Make.sh >/dev/null 2>&1 && bad "plain assembly wrote a script" || ok
"$BIN" game.asm 2>/dev/null; ls Make.sh game.xml >/dev/null 2>&1 && bad "plain assembly wrote a script or a definition" || ok

# --- the scripts
fresh
"$BIN" --build-scripts game.asm 2>/dev/null && cmp -s Make.sh "$HERE/rom/golden/Make.sh" && ok || bad "Make.sh (golden)"
cmp -s Make.bat "$HERE/rom/golden/Make.bat" && ok || bad "Make.bat (golden)"
[ -x Make.sh ] && ok || bad "Make.sh is executable"
grep -q "$(printf '\r')" Make.bat && ok || bad "Make.bat has CRLF line ends"
cmp -s game.xml "$HERE/rom/golden/game.xml" && cmp -s gameOpt.xml "$HERE/rom/golden/gameOpt.xml" && ok || bad "--build-scripts writes both definitions"
./Make.sh >"$T/run.log" 2>&1 && grep -q 'BUILD SUCCESSFUL' "$T/run.log" && [ -f bin/game.v32 ] && [ -f bin/gameOpt.v32 ] && ok || bad "Make.sh builds both cartridges"
grep -q '^v32opt -O3 gamePre.asm -o gameOpt.asm' "$STUBLOG" && grep -q '^png2vircon gfx/bg.png -o bg.vtex' "$STUBLOG" \
  && grep -q '^wav2vircon snd/boom.wav -o boom.vsnd' "$STUBLOG" && grep -q '^packrom gameOpt.xml -o bin/gameOpt.v32' "$STUBLOG" && ok || bad "the tools are called as written"
[ -f gfx/pre.vtex ] && ! grep -q 'png2vircon gfx/pre' "$STUBLOG" && ok || bad "a .vtex is used as it is"
# from another directory
rm -rf bin; ( cd / && "$T/p/Make.sh" >/dev/null 2>&1 ) && [ -f bin/game.v32 ] && ok || bad "Make.sh can be run from anywhere"
# the optimizer is given the preprocessed source: it does not know %macro, %if, %rep
fresh
printf '%%macro two 1\n  iadd %%1, 1\n  iadd %%1, 1\n%%endmacro\n%%title "M"\n_m:\n  two R0\n%%if 1\n  mov R1, 5\n%%else\n  mov R1, 6\n%%endif\n%%rep 2\n  iadd R1, 1\n%%endrep\n  hlt\n' > mac.asm
"$BIN" --build-scripts mac.asm 2>/dev/null; ./Make.sh >"$T/run.log" 2>&1
grep -q '^v32opt -O3 macPre.asm -o macOpt.asm' "$STUBLOG" && [ -f macPre.asm ] && ! grep -q '%' macPre.asm && grep -q 'mov R1, 5' macPre.asm && ! grep -q 'mov R1, 6' macPre.asm && ok || bad "v32opt reads the preprocessed source"
[ -f bin/mac.v32 ] && [ -f bin/macOpt.v32 ] && ok || bad "a program with macros builds both cartridges"
grep -q -- '-E -P -c mac.asm -o macPre.asm' Make.sh && ok || bad "the preprocessing step is in the script"
fresh; "$BIN" -g -D X=1 --build-scripts game.asm 2>/dev/null
grep -q -- '-E -P -c -D X=1 game.asm -o gamePre.asm' Make.sh && ! grep -- '-E -P' Make.sh | grep -q -- '--debug' && ok || bad "the preprocessing step has -D but not -g"
fresh; "$BIN" --build-scripts game.asm 2>/dev/null; printf '%%if\n' > game.asm
./Make.sh >"$T/run.log" 2>&1 && bad "a source that no longer assembles built" || { grep -q 'BUILD FAILED' "$T/run.log" && ok || bad "BUILD FAILED for a broken source"; }
fresh; "$BIN" --build-scripts game.asm 2>/dev/null
V32OPT_FLAGS=-O1 ./Make.sh >/dev/null 2>&1; grep -q '^v32opt -O1 gamePre.asm' "$STUBLOG" && ok || bad "V32OPT_FLAGS changes the optimization level"
# no optimizer / optimizer that fails
rm -rf bin gameOpt.*; NO_OPT=1 ./Make.sh >"$T/run.log" 2>&1 && [ -f bin/game.v32 ] && [ ! -f bin/gameOpt.v32 ] && grep -q 'skipping the optimized' "$T/run.log" && ok || bad "NO_OPT: only the regular cartridge"
rm -rf bin gameOpt.*; OPTFAIL=1 ./Make.sh >"$T/run.log" 2>&1 && [ -f bin/game.v32 ] && [ ! -f bin/gameOpt.v32 ] && grep -q 'v32opt failed' "$T/run.log" && ok || bad "a failing v32opt does not fail the build"
rm -rf bin; V32OPT=nosuchoptimizer ./Make.sh >"$T/run.log" 2>&1 && [ -f bin/game.v32 ] && [ ! -f bin/gameOpt.v32 ] && ok || bad "v32opt not installed: only the regular cartridge"
V32AS=nosuchassembler ./Make.sh >"$T/run.log" 2>&1 && bad "a missing assembler passed" || { grep -q 'nosuchassembler is needed' "$T/run.log" && grep -q 'BUILD FAILED' "$T/run.log" && ok || bad "missing tool message"; }
# a conversion that fails fails the build
mv gfx/bg.png gfx/bg.gone; ./Make.sh >"$T/run.log" 2>&1 && bad "a failed conversion passed" || { grep -q 'BUILD FAILED' "$T/run.log" && ok || bad "BUILD FAILED"; }
mv gfx/bg.gone gfx/bg.png

# --- scripts that exist
fresh
printf '#!/bin/sh\necho mine\n' > Make.sh; printf '@echo mine\r\n' > Make.bat; cp Make.sh m.sh; cp Make.bat m.bat
"$BIN" --build-scripts game.asm 2>"$T/err"; cmp -s Make.sh m.sh && cmp -s Make.bat m.bat && ok || bad "a hand-written Make.sh / Make.bat is never replaced"
grep -q 'Make.sh exists and was not generated by v32as' "$T/err" && ok || bad "... and says so"
"$BIN" --no-build-scripts --rom-xml game.xml game.asm 2>"$T/err"; grep -q 'leaving it alone' "$T/err" && bad "--no-build-scripts still complained" || ok
printf '#!/bin/bash\n# Generated by v32cc. Edit the sources\necho cc\n' > Make.sh; cp Make.sh m.sh
"$BIN" --build-scripts=sh game.asm 2>/dev/null; cmp -s Make.sh m.sh && ok || bad "a v32cc script is not replaced"
printf '#!/bin/sh\necho "Generated by v32as" is only a phrase here\n' > Make.sh; cp Make.sh m.sh
"$BIN" --build-scripts=sh game.asm 2>/dev/null; cmp -s Make.sh m.sh && ok || bad "the marker must open a comment line"
rm -f Make.sh Make.bat
"$BIN" --build-scripts=sh game.asm 2>/dev/null; [ -f Make.sh ] && [ ! -f Make.bat ] && ok || bad "--build-scripts=sh writes only Make.sh"
"$BIN" --build-scripts=bat game.asm 2>/dev/null; [ -f Make.bat ] && ok || bad "--build-scripts=bat"
# unchanged: not even rewritten
I=$(inode Make.sh); "$BIN" --build-scripts game.asm 2>/dev/null; [ "$(inode Make.sh)" = "$I" ] && ok || bad "an unchanged script is left alone"
# the environment variable the scripts set
echo '# my edit' >> Make.sh; cp Make.sh m.sh
V32AS_BUILD_SCRIPT=1 "$BIN" --build-scripts game.asm 2>/dev/null; cmp -s Make.sh m.sh && ok || bad "V32AS_BUILD_SCRIPT: scripts are not touched"
[ -f game.xml ] || bad "... but the definition is still written"
"$BIN" --build-scripts game.asm 2>/dev/null; cmp -s Make.sh m.sh && bad "without it the script is regenerated" || ok
# running the script does not rewrite it (it calls v32as with --no-build-scripts), and runs of v32as from it see the guard
cp Make.sh m.sh; I=$(inode Make.sh); ./Make.sh >/dev/null 2>&1; cmp -s Make.sh m.sh && [ "$(inode Make.sh)" = "$I" ] && ok || bad "running Make.sh leaves it alone"
grep -q -- '--no-build-scripts' Make.sh && grep -q 'export V32AS_BUILD_SCRIPT=1' Make.sh && ok || bad "Make.sh protects itself"
# replacing a script while something is reading it: the reader keeps the text it opened (the new script is another file)
cp Make.sh old.sh; exec 3< Make.sh
"$BIN" --build-scripts -D HELD=1 game.asm 2>/dev/null
cat <&3 > held.sh; exec 3<&-
grep -q 'HELD' Make.sh && ok || bad "the script was replaced"
cmp -s held.sh old.sh && ok || bad "a reader of the old script keeps the old text (the replacement is atomic)"
cp old.sh Make.sh
# the same with a shell that is running it: the running copy finishes
sed 's|^echo Pack the ROM|sleep 2; echo Pack the ROM|' Make.sh > slow && cat slow > Make.sh
( ./Make.sh >"$T/slow.log" 2>&1; echo "exit=$?" >> "$T/slow.log" ) &
sleep 1
"$BIN" --build-scripts -D CHANGED=1 game.asm 2>/dev/null
wait
grep -q 'CHANGED' Make.sh && ok || bad "the script was replaced while running"
grep -q 'BUILD SUCCESSFUL' "$T/slow.log" && grep -q 'exit=0' "$T/slow.log" && ok || bad "the running copy finished"

# --- layouts and other inputs
fresh
mkdir -p "src/my gfx" obj; : > "src/my gfx/a b.png"
printf '%%title "T"\n%%texture "my gfx/a b.png"\n%%include "defs.inc"\n_x: hlt\n' > src/main.asm; echo '%define Z 1' > src/defs.inc
"$BIN" -I src -D FOO=1 -g --build-scripts -o obj/main.vbin src/main.asm 2>/dev/null
[ -f Make.sh ] && [ ! -f src/Make.sh ] && ok || bad "src/ + obj/: the scripts go to the parent"
grep -q '"\$V32AS" -c -D FOO=1 -I src --debug=program src/main.asm -o obj/main.vbin --rom-xml obj/main.xml --rom-xml-opt obj/mainOpt.xml' Make.sh && ok || bad "the options are repeated, paths from the script's directory"
grep -q "'src/my gfx/a b.png' -o 'obj/a b.vtex'" Make.sh && ok || bad "names with spaces are quoted"
grep -q '"\$V32AS" -c -D FOO=1 -I src --debug=program obj/mainOpt.asm' Make.sh && ok || bad "the Opt assembly reads %include through the source's directory (once)"
./Make.sh >"$T/run.log" 2>&1 && [ -f bin/main.v32 ] && [ -f obj/main.vbin.debug ] && [ -f obj/mainOpt.vbin.debug ] && ok || bad "that script builds"
fresh; printf '_a: hlt\n' > a.asm; printf '_b: hlt\n' > b.asm
"$BIN" --build-scripts=sh -o two.vbin a.asm b.asm 2>/dev/null; grep -q 'V32OPT\|Opt' Make.sh | grep -v '^#' ; grep -v '^#' Make.sh | grep -q 'Opt' && bad "two sources: no optimized twin" || ok
./Make.sh >/dev/null 2>&1 && [ -f bin/two.v32 ] && ok || bad "two sources build"
fresh; "$BIN" --build-scripts --build-script-dir "$T/p/out" game.asm 2>/dev/null; mkdir -p out; "$BIN" --build-scripts --build-script-dir out game.asm 2>/dev/null
[ -f out/Make.sh ] && grep -q '\.\./game.asm' out/Make.sh && ok || bad "--build-script-dir"
# ROM directives with a source that is included
fresh; printf '%%texture T "gfx/bg.png"\n' > res.inc; printf '%%include "res.inc"\n_m: mov R0, T\n  hlt\n' > inc.asm
"$BIN" --rom-xml inc.xml inc.asm 2>/dev/null; grep -q 'path="bg.vtex"' inc.xml && ok || bad "%texture inside an included file"

echo "rom tests: passed $pass, failed $fail"
[ "$fail" = 0 ]
