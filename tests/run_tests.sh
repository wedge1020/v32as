#!/bin/sh
# usage: run_tests.sh path/to/v32asc   (needs only sh, od, cmp, grep)
BIN=${1:-../bin/v32asc}
cd "$(dirname "$0")" || exit 1
case $BIN in /*) ;; *) BIN=$OLDPWD/$BIN ;; esac
LD=${2:-$(dirname "$BIN")/v32asld}
case $LD in /*) ;; *) LD=$OLDPWD/$LD ;; esac
T=${TMPDIR:-/tmp}/v32asc_test.$$; mkdir -p "$T"; trap 'rm -rf "$T"' EXIT
pass=0; fail=0
ok()  { pass=$((pass+1)); }
bad() { fail=$((fail+1)); echo "FAIL: $1"; }
for g in golden/*.hex; do
  b=$(basename "$g" .hex); name=${b%.*}; mode=${b##*.}
  "$BIN" --$mode --binary -o "$T/o.vbin" cases/$name.asm 2>"$T/err" || { bad "$b (assemble)"; continue; }
  od -An -tx4 -v -w4 "$T/o.vbin" > "$T/o.hex"
  cmp -s "$T/o.hex" "$g" && ok || bad "$b (output differs)"
done
for g in golden/*.debug; do
  b=$(basename "$g" .debug); name=${b%.*}; mode=${b##*.}
  ( cd cases && "$BIN" --$mode -B -g -o "$T/d.vbin" $name.asm ) 2>/dev/null || { bad "$b (assemble -g)"; continue; }
  cmp -s "$T/d.vbin.debug" "$g" && ok || bad "$b (debug output differs)"
done
for e in errors/*.asm; do
  n=$(basename "$e" .asm)
  if "$BIN" --none "$e" >/dev/null 2>"$T/err"; then bad "$n (expected failure)"
  elif grep -q "$(cat errors/$n.expect)" "$T/err"; then ok
  else bad "$n (message mismatch)"; fi
done
# preprocessor: assembling the source must equal assembling its hand-expanded form (.ref)
( cd pp && for f in *.asm; do
    n=${f%.asm}
    "$BIN" --cart -B -I incs -o "$T/pa.vbin" "$f" 2>"$T/err" || { echo "FAIL: pp/$n (assemble)"; continue; }
    "$BIN" --cart -B -o "$T/pb.vbin" "$n.ref" 2>"$T/err" || { echo "FAIL: pp/$n.ref (assemble)"; continue; }
    cmp -s "$T/pa.vbin" "$T/pb.vbin" || echo "FAIL: pp/$n (differs from $n.ref)"
  done ) > "$T/ppout"
cat "$T/ppout"
for f in pp/*.asm; do pass=$((pass+1)); done
pass=$((pass - $(grep -c '^FAIL' "$T/ppout")))
fail=$((fail + $(grep -c '^FAIL' "$T/ppout")))
# preprocessor: -E output (-P: no line markers when the .out has none)
for f in pp/expand/*.asm; do
  n=${f%.asm}
  ( cd pp && "$BIN" -E "${f#pp/}" ) > "$T/e.out" 2>"$T/err"
  cmp -s "$T/e.out" "$n.out" && ok || { bad "$n (-E output differs)"; diff "$n.out" "$T/e.out" | head -5; }
done
# preprocessor: -D/-U, and the .debug file names the included file and its own line numbers
printf '%%ifdef FLAG\n  mov R0, FLAG\n%%endif\n' > "$T/d.asm"
"$BIN" -E -P -D FLAG=42 "$T/d.asm" 2>/dev/null | grep -q 'mov R0, 42' && ok || bad "-D"
"$BIN" -E -P -D FLAG=42 -U FLAG "$T/d.asm" 2>/dev/null | grep -q 'mov R0' && bad "-U" || ok
( cd pp && "$BIN" --cart -B -g -o "$T/g.vbin" dbg_include.asm ) 2>/dev/null
cmp -s "$T/g.vbin.debug" pp/dbg_include.debug && ok || bad "pp/dbg_include (.debug differs)"
( cd pp && "$BIN" --cart -B -g -o "$T/g.vbin" dbg_macro.asm ) 2>/dev/null
cmp -s "$T/g.vbin.debug" pp/dbg_macro.debug && ok || bad "pp/dbg_macro (.debug differs)"
# preprocessor: -E output under --standard
for f in pp/stdexpand/*.asm; do
  n=${f%.asm}
  ( cd pp && "$BIN" --standard -E "${f#pp/}" ) > "$T/e.out" 2>"$T/err"
  cmp -s "$T/e.out" "$n.out" && ok || { bad "$n (--standard -E output differs)"; diff "$n.out" "$T/e.out" | head -5; }
done
# --standard: the language of the official assembler, nothing more
for e in std/*.expect; do
  n=$(basename "$e" .expect)
  if "$BIN" --standard --none "std/$n.asm" >/dev/null 2>"$T/err"; then bad "std/$n (expected failure)"
  elif grep -q "$(cat $e)" "$T/err"; then ok
  else bad "std/$n (message mismatch)"; fi
done
for f in std/*.ref; do
  n=${f%.ref}
  "$BIN" --standard --cart -o "$T/sa.vbin" "$n.asm" 2>"$T/err" || { bad "$n (--standard assemble)"; continue; }
  "$BIN" --standard --cart -o "$T/sb.vbin" "$f" 2>"$T/err"    || { bad "$f (--standard assemble)"; continue; }
  cmp -s "$T/sa.vbin" "$T/sb.vbin" && ok || bad "$n (--standard differs from its .ref)"
done
# label +/- constant, the relocation list (-r) and the compiler debug map (--map)
printf '  mov R0, _t+2\n  mov R1, [_t-1]\n  pointer _t+1\n_t: hlt\n' > "$T/lo.asm"
"$BIN" --cart -w -o "$T/lo.vbin" "$T/lo.asm" && od -An -tx4 -v -w4 "$T/lo.vbin" | tail -n +4 | tr -d '\n' \
  | grep -q '^ 4e000000 20000007 4e208000 20000004 20000006 00000000$' && ok || bad "label + constant"
"$BIN" --standard --none "$T/lo.asm" >/dev/null 2>&1 && bad "label + constant under --standard" || ok
( cd map && "$BIN" --cart -w -o "$T/r.vbin" -r "$T/r.rel" labeloff.asm ) 2>/dev/null
cmp -s "$T/r.rel" map/labeloff.rel && ok || bad "-r relocation list differs"
( cd map && "$BIN" --cart -w -g --map labeloff.map -o "$T/m.vbin" labeloff.asm ) 2>/dev/null
cmp -s "$T/m.vbin.debug" map/labeloff.debug && ok || bad "--map debug file differs"
# objects and the linker: linking must equal assembling the same code as one file
( cd link
  "$BIN" -C -o "$T/m1.v32ob" m1.asm && "$BIN" -C -o "$T/m2.v32ob" m2.asm \
    || { echo "FAIL: -C (assemble)"; exit 1; }
  for mode in "-c" "-b" "-O 0x1000"; do
    "$LD" $mode -o "$T/l.vbin" "$T/m1.v32ob" "$T/m2.v32ob" 2>/dev/null \
      && "$BIN" $mode -o "$T/a.vbin" all.asm 2>/dev/null \
      && cmp -s "$T/l.vbin" "$T/a.vbin" && echo "ok" || echo "FAIL: link $mode differs from one-file assembly"
  done
  "$LD" -g -o "$T/g.vbin" "$T/m1.v32ob" "$T/m2.v32ob" 2>/dev/null
  cmp -s "$T/g.vbin.debug" link.debug && echo ok || echo "FAIL: linked .debug differs"
  "$LD" -o "$T/x.vbin" "$T/m1.v32ob" 2>"$T/err" && echo "FAIL: undefined symbol accepted" \
    || { grep -q "undefined reference to \`_table' (first used in .*m1.v32ob)" "$T/err" && echo ok || echo "FAIL: undefined symbol message"; }
  "$LD" -o "$T/x.vbin" "$T/m2.v32ob" "$T/m2.v32ob" 2>"$T/err" && echo "FAIL: duplicate symbol accepted" \
    || { grep -q "defined in both" "$T/err" && echo ok || echo "FAIL: duplicate symbol message"; }
) > "$T/ldout" 2>&1
cat "$T/ldout" | grep '^FAIL'
pass=$((pass + $(grep -c '^ok' "$T/ldout")))
fail=$((fail + $(grep -c '^FAIL' "$T/ldout")))
printf 'extern _x\n_x:\n' > "$T/ex.asm"
"$BIN" -C -o "$T/ex.v32ob" "$T/ex.asm" 2>"$T/err" && bad "extern label defined" || { grep -q "declared extern" "$T/err" && ok || bad "extern message"; }
printf 'global _x\n' > "$T/gl.asm"
"$BIN" -C -o "$T/gl.v32ob" "$T/gl.asm" 2>"$T/err" && bad "global never defined" || { grep -q "gl.asm:1:8: error: global label" "$T/err" && ok || bad "global error location"; }
"$BIN" --standard -C -o "$T/x.v32ob" "$T/lo.asm" 2>/dev/null && bad "--object under --standard" || ok
printf 'global _x\n_x: hlt\n' > "$T/gs.asm"
"$BIN" --standard --none "$T/gs.asm" >/dev/null 2>&1 && bad "global under --standard" || ok
# command line: binary output by default, as the official assembler does
printf '  mov R0, 0100\n  hlt\n' > "$T/x.asm"
"$BIN" -w "$T/x.asm" 2>"$T/err" && [ -f "$T/x.vbin" ] && ok || bad "default output file x.vbin"
"$BIN" --cart -B -w -o "$T/y.vbin" "$T/x.asm" && cmp -s "$T/x.vbin" "$T/y.vbin" && ok || bad "default is a CART binary"
printf '_a:\n  mov R0, _a\n' > "$T/b.asm"
"$BIN" -o "$T/b1.vbin" "$T/b.asm" && "$BIN" -b -o "$T/b2.vbin" "$T/b.asm" && ! cmp -s "$T/b1.vbin" "$T/b2.vbin" && ok || bad "-b changes the base address"
"$BIN" -T "$T/x.asm" 2>/dev/null | grep -q '^instruction:' && ok || bad "-T prints a listing"
"$BIN" -T "$T/x.asm" 2>/dev/null | grep -q OFFSET && bad "-T alone has no base address" || ok
"$BIN" -B -T "$T/x.asm" >/dev/null 2>&1 && bad "-B with -T must fail" || ok
"$BIN" "$T/x.asm" 2>&1 >/dev/null | grep -q "octal here" && ok || bad "octal literals warn"
"$BIN" -w "$T/x.asm" 2>&1 >/dev/null | grep -q . && bad "-w silences warnings" || ok
"$BIN" --standard "$T/x.asm" -o "$T/s.vbin" 2>&1 | grep -q . && bad "--standard reads 0100 as decimal, silently" || ok
od -An -tx4 -v -w4 "$T/s.vbin" | sed -n 5p | grep -q 00000064 && ok || bad "--standard: 0100 is 100"
"$BIN" --standard "$T/x.asm" "$T/x.asm" >/dev/null 2>&1 && bad "--standard takes a single input file" || ok
"$BIN" --standard -v "$T/x.asm" 2>/dev/null | grep -q '^assembly successful' && ok || bad "--standard -v prints the official messages"
printf 'hlt\n' | "$BIN" - | od -An -c | head -1 | grep -q 'V   3   2   -   V   B   I   N' && ok || bad "stdin -> binary on stdout"
# stdin batch mode and legacy bare word
printf 'HLT\n' | "$BIN" - >/dev/null 2>&1 && ok || bad "stdin"
# VA-1: datafile looks in the file's directory, then each -I, then the current directory
mkdir -p "$T/src" "$T/dat"
printf '\001\000\000\000\002\000\000\000' > "$T/dat/romfs.img"
printf '_d: datafile "romfs.img"\n' > "$T/src/d.asm"
( cd "$T/src" && "$BIN" -w -o "$T/d1.vbin" d.asm ) 2>"$T/err" && bad "datafile found without -I" \
  || { grep -q 'cannot open data file "romfs.img" (looked in .*)' "$T/err" && ok || bad "datafile error lists places"; }
( cd "$T/src" && "$BIN" -w -I "$T/dat" -o "$T/d2.vbin" d.asm ) 2>/dev/null && ok || bad "datafile via -I"
( cd "$T/dat" && "$BIN" -w -o "$T/d3.vbin" "$T/src/d.asm" ) 2>/dev/null && ok || bad "datafile in the current directory"
# VA-3: -D against a file's %define
printf '%%define X 5\n  mov R0, X\n' > "$T/dx.asm"
"$BIN" -T -D X=9 "$T/dx.asm" 2>&1 | grep -q "redefines the -D definition" && ok || bad "%define over -D warns"
"$BIN" -T -D X=9 "$T/dx.asm" 2>/dev/null | grep -q 'immediate:   0x00000005' && ok || bad "%define over -D: the file wins"
"$BIN" -T -D '!X=9' "$T/dx.asm" 2>&1 | grep -q 'immediate:   0x00000009' && ok || bad "-D! makes the command line win"
"$BIN" -T --force-define X=9 "$T/dx.asm" 2>&1 | grep -q 'immediate:   0x00000009' && ok || bad "--force-define"
printf '%%define A 1\n%%define A 2\n  mov R0, A\n' > "$T/rd.asm"
"$BIN" -T "$T/rd.asm" 2>&1 | grep -q "Wredefine" && ok || bad "redefinition warns"
"$BIN" -T -Wno-redefine "$T/rd.asm" 2>&1 | grep -q "redefined" && bad "-Wno-redefine" || ok
"$BIN" -T --standard "$T/rd.asm" 2>&1 | grep -q "redefined" && bad "no -Wredefine under --standard" || ok
printf '%%define A 1\n%%define A 1\n  hlt\n' > "$T/rd2.asm"
"$BIN" -T "$T/rd2.asm" 2>&1 | grep -q "redefined" && bad "same value is no redefinition" || ok
# VA-2: --symbols
printf '_s:\n  mov R0, 1\n.l:\n  jmp .l\n_t:\n  integer 1\n%%define global_v 0x1000\n%%define other 3\n' > "$T/sy.asm"
"$BIN" -w --symbols "$T/sy.sym" -o "$T/sy.vbin" "$T/sy.asm" \
  && [ "$(cat "$T/sy.sym")" = "$(printf '0x20000000,_s,F\n0x20000004,_t,D')" ] && ok || bad "--symbols"
"$BIN" -w --symbols "$T/sy.sym" --symbols-all -o "$T/sy.vbin" "$T/sy.asm" \
  && [ "$(cat "$T/sy.sym")" = "$(printf '0x20000000,_s,F\n0x20000002,_s.l,F\n0x20000004,_t,D\n0x00001000,global_v,V' | sort -t, -k1,1)" ] && ok || bad "--symbols-all"
# --symbols-all: "datasym+N" references are variables of kind V, the offset N in the address column
printf 'datasym __data_base\n_s:\n  mov R0, [__data_base+3]\n  mov R1, [__data_base+1]\n  mov [__data_base+3], R0\n' > "$T/dv.asm"
"$BIN" -w --symbols "$T/dv.sym" -o "$T/dv.vbin" "$T/dv.asm" \
  && [ "$(cat "$T/dv.sym")" = "$(printf '0x20000000,_s,F')" ] && ok || bad "--symbols leaves out datasym variables"
"$BIN" -w --symbols "$T/dv.sym" --symbols-all -o "$T/dv.vbin" "$T/dv.asm" \
  && [ "$(cat "$T/dv.sym")" = "$(printf '0x00000001,__data_base+1,V\n0x00000003,__data_base+3,V\n0x20000000,_s,F')" ] && ok || bad "--symbols-all lists datasym+N as V"
# VA-4: --map matches the assembly file whatever its spelling, and says when nothing matched
mkdir -p "$T/out/k"
printf '_f:\n  hlt\n  hlt\n' > "$T/out/k/main.asm"
printf 'asmfile,asmline,cfile,cline,function\nmain.asm,2,a.c,33,f\nmain.asm,3,a.c,34\n' > "$T/mm.map"
"$BIN" --cart -w -g --map "$T/mm.map" -o "$T/out/k/m.vbin" "$T/out/k/main.asm" 2>/dev/null
[ "$(wc -l < "$T/out/k/m.vbin.debug")" = 2 ] && ok || bad "--map with a differently spelled file name"
printf 'zzz.asm,2,a.c,3\n' > "$T/nm.map"
"$BIN" --cart -g --map "$T/nm.map" -o "$T/out/k/n.vbin" "$T/out/k/main.asm" 2>&1 | grep -q "no entry matches" && ok || bad "--map total miss warns"
# VA-6: dependency output
printf '%%include "inc.inc"\n_d: datafile "romfs.img"\n' > "$T/src/dd.asm"
printf 'hlt\n' > "$T/src/inc.inc"
( cd "$T/src" && "$BIN" -w -MD -MP -I "$T/dat" -o "$T/dd.vbin" dd.asm ) 2>/dev/null
grep -q '^'"$T"'/dd.vbin:' "$T/dd.d" && grep -q 'inc.inc' "$T/dd.d" && grep -q 'romfs.img' "$T/dd.d" && grep -q '^inc.inc:' "$T/dd.d" \
  && ok || bad "-MD / -MP"
( cd "$T/src" && "$BIN" -w -MD -MF "$T/o.d" -MT cart.vbin -I "$T/dat" -o "$T/dd.vbin" dd.asm ) 2>/dev/null
head -1 "$T/o.d" | grep -q '^cart.vbin:' && ok || bad "-MF / -MT"
# VA-5: archives, --entry, --symbols and the undefined-reference message in v32asld
printf 'global _main\nextern _lib\n_main:\n  call _lib\n  hlt\n' > "$T/am.asm"
printf 'global _lib\n_lib:\n  ret\n' > "$T/al.asm"
printf 'global _unused\n_unused:\n  hlt\n' > "$T/au.asm"
for n in am al au; do "$BIN" -C -o "$T/$n.v32ob" "$T/$n.asm" 2>/dev/null; done
"$LD" -A "$T/libx.a" "$T/al.v32ob" "$T/au.v32ob" && ok || bad "v32asld -A"
"$LD" -o "$T/ar1.vbin" "$T/am.v32ob" "$T/libx.a" --symbols "$T/ar1.sym" 2>/dev/null \
  && grep -q '_lib,F' "$T/ar1.sym" && ! grep -q '_unused' "$T/ar1.sym" && ok || bad "archive member pulled only when needed"
"$LD" -o "$T/ar2.vbin" "$T/am.v32ob" -L "$T" -l x 2>/dev/null && cmp -s "$T/ar1.vbin" "$T/ar2.vbin" && ok || bad "-L / -l"
"$LD" -o "$T/ar3.vbin" "$T/am.v32ob" 2>&1 | grep -q "undefined reference to \`_lib' (first used in" && ok || bad "undefined reference message"
"$LD" -o "$T/en.vbin" --entry _lib "$T/am.v32ob" "$T/libx.a" -r "$T/en.rel" 2>/dev/null \
  && od -An -tx4 -v -w4 "$T/en.vbin" | sed -n 4,5p | tr -d '\n ' | grep -q '^0a00000020000005' && ok || bad "--entry places a jump"
# V32X output (v32nix): 8-word header, then the image linked at the window base
"$LD" --v32x --data 7 --entry _lib -o "$T/x.x" "$T/am.v32ob" "$T/al.v32ob" 2>/dev/null \
  && [ "$(od -An -tx4 -v -w4 "$T/x.x" | head -8 | tr -d ' \n')" = 5832335600000001000000080000000400000007000000030010000000000000 ] \
  && [ "$(od -An -tx4 -v -w4 "$T/x.x" | sed -n 10p | tr -d ' ')" = 00100003 ] && ok || bad "--v32x header and addresses"
"$LD" --v32x -o "$T/x.x" "$T/am.v32ob" "$T/al.v32ob" 2>/dev/null && bad "--v32x without --data" || ok
"$LD" --v32x --data 1 --entry 99 -o "$T/x.x" "$T/am.v32ob" "$T/al.v32ob" 2>/dev/null && bad "--v32x entry outside the image" || ok
mkdir -p "$T/lib"; cp "$T/libx.a" "$T/lib/x.v32a"
"$LD" -o "$T/ar4.vbin" "$T/am.v32ob" -L "$T/lib" -l x 2>/dev/null && cmp -s "$T/ar1.vbin" "$T/ar4.vbin" && ok || bad "-l finds NAME.v32a"
# VA-1 bug: a symbol's datasym flag was never initialised, so code relocations came out as ',D' (poisoned heap shows it)
for pert in 0 165 90; do
  ( cd reloc && MALLOC_PERTURB_=$pert "$BIN" -w -o "$T/k.vbin" -r "$T/k.rel" kind_D.asm ) 2>/dev/null
  cmp -s "$T/k.rel" reloc/kind_D.rel && ok || bad "reloc kinds with MALLOC_PERTURB_=$pert"
done
# VA-2/3/4/5: datasym and --map through objects, partial links (v32asld -R), --map in diagnostics
( cd link2
  "$BIN" -C -o "$T/d1.v32ob" d1.asm && "$BIN" -C -o "$T/d2.v32ob" d2.asm \
    && "$LD" -o "$T/ds.vbin" -r "$T/ds.rel" "$T/d1.v32ob" "$T/d2.v32ob" 2>/dev/null \
    && cmp -s "$T/ds.rel" ds.rel && echo ok || echo "FAIL: datasym through objects (-r kinds)"
  "$LD" -R -o "$T/dp.v32ob" "$T/d1.v32ob" "$T/d2.v32ob" 2>/dev/null \
    && "$LD" -o "$T/dp.vbin" -r "$T/dp.rel" "$T/dp.v32ob" 2>/dev/null \
    && cmp -s "$T/dp.vbin" "$T/ds.vbin" && cmp -s "$T/dp.rel" ds.rel && echo ok || echo "FAIL: partial link of datasym objects"
  "$BIN" -C --map c.map -o "$T/c1.v32ob" c1.asm && "$BIN" -C --map c.map -o "$T/c2.v32ob" c2.asm \
    && "$LD" -g -o "$T/cm.vbin" "$T/c1.v32ob" "$T/c2.v32ob" 2>/dev/null \
    && cmp -s "$T/cm.vbin.debug" cmap.debug && echo ok || echo "FAIL: -C --map then v32asld -g"
  "$BIN" -g --map one.map -o "$T/one.vbin" one.asm 2>/dev/null \
    && cmp -s "$T/one.vbin.debug" cmap.debug && echo ok || echo "FAIL: -g --map on the joined source"
  "$LD" -R -o "$T/cp.v32ob" "$T/c1.v32ob" "$T/c2.v32ob" 2>/dev/null \
    && "$LD" -g -o "$T/cp.vbin" "$T/cp.v32ob" 2>/dev/null \
    && cmp -s "$T/cp.vbin.debug" cmap.debug && echo ok || echo "FAIL: partial link keeps the map records"
  "$LD" -R -o "$T/c1p.v32ob" "$T/c1.v32ob" 2>/dev/null \
    && "$LD" -R -o "$T/cq.v32ob" "$T/c1p.v32ob" "$T/c2.v32ob" 2>/dev/null \
    && "$LD" -o "$T/cq.vbin" "$T/cq.v32ob" 2>/dev/null && cmp -s "$T/cq.vbin" "$T/cm.vbin" && echo ok \
    || echo "FAIL: two-step partial link differs from the direct link"
  "$LD" -R -g -o "$T/x.v32ob" "$T/c1.v32ob" "$T/c2.v32ob" 2>/dev/null && echo "FAIL: -R with -g accepted" || echo ok
  # a diagnostic names the C line: exact entry, or the nearest earlier one
  printf 'u.asm,3,u.c,5,main\n' > "$T/u.map"
  printf '_start:\n  mov R0, 1\n  mov R1, _nowhere\n  jmp _nowhere2\n' > "$T/u.asm"
  "$BIN" --map "$T/u.map" -o "$T/u.vbin" "$T/u.asm" 2>&1 | grep -q 'u.c:5: note: .* generated from here' && echo ok || echo "FAIL: --map note for a diagnostic"
  "$BIN" --map "$T/u.map" -o "$T/u.vbin" "$T/u.asm" 2>&1 | grep -q 'u.c:5: note: .* follows the code generated from here' && echo ok || echo "FAIL: --map note for a later line"
) > "$T/va2out" 2>&1
grep '^FAIL' "$T/va2out"
pass=$((pass + $(grep -c '^ok' "$T/va2out")))
fail=$((fail + $(grep -c '^FAIL' "$T/va2out")))
# ;@src markers: -g (and -C) take the entries from them; --map wins for the instructions it names
( cd markers
  "$BIN" --cart -B -g -o "$T/mk.vbin" bench.asm 2>/dev/null && cmp -s "$T/mk.vbin.debug" bench.debug && echo ok || echo "FAIL: ;@src -g entries"
  "$BIN" --cart -B -g -o "$T/mk.vbin" bench.asm 2>/dev/null; tail -n +2 "$T/mk.vbin.debug" > "$T/mk1.debug"   # without the "# lang lua" line (--map has none)
  "$BIN" --cart -B -g --map bench.map -o "$T/mk.vbin" bench.asm 2>/dev/null && cmp -s "$T/mk.vbin.debug" "$T/mk1.debug" && echo ok || echo "FAIL: ;@src -g equals --map of the same lines"
  "$BIN" --cart -B -g --map bench_mapwins.map -o "$T/mk.vbin" bench.asm 2>/dev/null && cmp -s "$T/mk.vbin.debug" bench_mapwins.debug && echo ok || echo "FAIL: --map wins over ;@src"
  "$BIN" --cart -B -g vbin -o "$T/mk.vbin" bench.asm 2>/dev/null && cmp -s "$T/mk.vbin.debug" bench_vbin.debug && echo ok || echo "FAIL: ;@src with -g vbin"
  "$BIN" --cart -B -g -o "$T/mk.vbin" edge.asm 2>/dev/null && cmp -s "$T/mk.vbin.debug" edge.debug && echo ok || echo "FAIL: ;@src edge cases (CRLF, trailing comment, two in a row)"
  "$BIN" --cart -B -g -o "$T/mk.vbin" main2.asm 2>/dev/null && cmp -s "$T/mk.vbin.debug" include.debug && echo ok || echo "FAIL: ;@src across %include"
  # the markers do not change the program, and under --standard they are plain comments (assembly lines)
  grep -v '^[[:space:]]*;@src' bench.asm > "$T/nomk.asm"
  "$BIN" --cart -B -o "$T/mk.vbin" bench.asm 2>/dev/null && "$BIN" --cart -B -o "$T/nomk.vbin" "$T/nomk.asm" 2>/dev/null \
    && cmp -s "$T/mk.vbin" "$T/nomk.vbin" && echo ok || echo "FAIL: ;@src changes the output"
  "$BIN" --standard --cart -B -g -o "$T/mk.vbin" bench.asm 2>/dev/null && grep -q '^0x20000000,bench.asm,2$' "$T/mk.vbin.debug" \
    && [ "$(wc -l < "$T/mk.vbin.debug")" -gt 4 ] && echo ok || echo "FAIL: --standard ignores ;@src"
  # -C: the object stores the entries; v32asld -g writes the same file (and no invented label for a marker without function)
  "$BIN" -C -o "$T/mk.v32ob" bench.asm 2>/dev/null && "$LD" -c -g -o "$T/mko.vbin" "$T/mk.v32ob" 2>/dev/null \
    && cmp -s "$T/mko.vbin.debug" bench.debug && echo ok || echo "FAIL: -C then v32asld -g with ;@src"
  "$BIN" -C -o "$T/n1.v32ob" nofn.asm 2>/dev/null && "$BIN" -C -o "$T/n2.v32ob" main2.asm 2>/dev/null \
    && "$LD" -c -g -o "$T/n.vbin" "$T/n1.v32ob" "$T/n2.v32ob" 2>/dev/null && "$LD" -R -o "$T/n3.v32ob" "$T/n1.v32ob" "$T/n2.v32ob" 2>/dev/null \
    && "$LD" -c -g -o "$T/n4.vbin" "$T/n3.v32ob" 2>/dev/null && cmp -s "$T/n.vbin.debug" "$T/n4.vbin.debug" \
    && [ "$(head -1 "$T/n.vbin.debug")" = "0x20000000,x.lua,1" ] && echo ok || echo "FAIL: ;@src through a link and a partial link"
  "$BIN" -C --map nofn.map -o "$T/n5.v32ob" nofn.asm 2>/dev/null && "$LD" -c -g -o "$T/n5.vbin" "$T/n5.v32ob" 2>/dev/null \
    && [ "$(cat "$T/n5.vbin.debug")" = "0x20000000,MAP.c,9,mf" ] && echo ok || echo "FAIL: -C --map wins over ;@src"
  # -Wmarkers: off by default; one warning per malformed line
  "$BIN" --cart -B -g -o "$T/mk.vbin" bad.asm 2>&1 | grep -q 'malformed' && echo "FAIL: -Wmarkers is off by default" || echo ok
  [ "$("$BIN" --cart -B -g -Wmarkers -o "$T/mk.vbin" bad.asm 2>&1 | grep -c 'malformed ;@src')" = 4 ] && echo ok || echo "FAIL: -Wmarkers warnings"
  [ "$(tail -1 "$T/mk.vbin.debug")" = "0x20000008,a.lua,9" ] && [ "$(grep -c ',-,0$' "$T/mk.vbin.debug")" = 4 ] && echo ok || echo "FAIL: a malformed ;@src is treated as ;@src -"
  # ;@src - gives the entry "ADDRESS,-,0" (v32sim ends the run of the line before it there)
  [ "$(grep -c '^0x2000000B,-,0$' bench.debug)" = 1 ] && echo ok || echo "FAIL: ;@src - entry"
  # the function: __function_NAME_return is the end of NAME, not a function
  "$BIN" --cart -B -g -o "$T/mk.vbin" ret.asm 2>/dev/null && cmp -s "$T/mk.vbin.debug" ret.debug && echo ok || echo "FAIL: __function_NAME_return is not a function"
) > "$T/mkout" 2>&1
grep '^FAIL' "$T/mkout"
pass=$((pass + $(grep -c '^ok' "$T/mkout")))
fail=$((fail + $(grep -c '^FAIL' "$T/mkout")))
# ;@var / ;@scope: FILE.vars (address ranges), through objects, links and partial links
( cd markers
  for n in lua_dbg lua_edge vars; do
    "$BIN" --cart -B -g -o "$T/$n.vbin" $n.asm 2>/dev/null \
      && cmp -s "$T/$n.vbin.debug" $n.debug && cmp -s "$T/$n.vbin.vars" $n.vars && echo ok || echo "FAIL: $n .debug / .vars"
  done
  for n in lua_dbg lua_edge; do
    "$BIN" -C -o "$T/$n.v32ob" $n.asm 2>/dev/null && "$LD" -c -g -o "$T/$n.l.vbin" "$T/$n.v32ob" 2>/dev/null \
      && cmp -s "$T/$n.l.vbin.vars" $n.vars && cmp -s "$T/$n.l.vbin.debug" $n.debug && echo ok || echo "FAIL: $n through an object and the linker"
    "$LD" -R -o "$T/$n.r.v32ob" "$T/$n.v32ob" 2>/dev/null && "$LD" -c -g -o "$T/$n.r.vbin" "$T/$n.r.v32ob" 2>/dev/null \
      && cmp -s "$T/$n.r.vbin.vars" $n.vars && echo ok || echo "FAIL: $n through a partial link"
  done
  # two files, one function each: linking equals assembling the joined source
  "$BIN" -C -o "$T/v1.v32ob" vl1.asm 2>/dev/null && "$BIN" -C -o "$T/v2.v32ob" vl2.asm 2>/dev/null \
    && "$LD" -c -g -o "$T/vl.vbin" "$T/v1.v32ob" "$T/v2.v32ob" 2>/dev/null \
    && cmp -s "$T/vl.vbin.vars" vl12.vars && cmp -s "$T/vl.vbin.debug" vl12.debug && echo ok || echo "FAIL: two files linked == the joined source"
  "$LD" -R -o "$T/v12.v32ob" "$T/v1.v32ob" "$T/v2.v32ob" 2>/dev/null && "$LD" -c -g -o "$T/vp.vbin" "$T/v12.v32ob" 2>/dev/null \
    && cmp -s "$T/vp.vbin.vars" vl12.vars && echo ok || echo "FAIL: two files partially linked"
  # the file modes, the object version, no records -> no file (and an old one goes), --standard
  "$BIN" --cart -B -g vbin -o "$T/vm.vbin" vl1.asm 2>/dev/null && head -1 "$T/vm.vbin.vars" | grep -q '^var,0x00000010,0x00000028,a,BP+2,lua,-,0,f$' && echo ok || echo "FAIL: .vars in -g vbin mode"
  [ "$(od -An -tx4 -j8 -N4 "$T/v1.v32ob" | tr -d ' ')" = 00000003 ] && [ "$(od -An -tx4 -j8 -N4 "$T/n1.v32ob" | tr -d ' ')" = 00000002 ] && echo ok || echo "FAIL: object versions (3 with ;@var, 2 with ;@src only)"
  printf 'old\n' > "$T/nv.vbin.vars"; "$BIN" --cart -B -g -o "$T/nv.vbin" nofn.asm 2>/dev/null; [ ! -e "$T/nv.vbin.vars" ] && echo ok || echo "FAIL: stale .vars is left"
  "$BIN" --standard --cart -B -g -o "$T/sv.vbin" vars.asm 2>/dev/null; [ ! -e "$T/sv.vbin.vars" ] && echo ok || echo "FAIL: --standard writes .vars"
  [ "$("$BIN" --cart -B -g -Wmarkers -o "$T/vw.vbin" vars.asm 2>&1 | grep -c 'warning')" = 2 ] && echo ok || echo "FAIL: -Wmarkers on ;@scope"
  # the instructions do not depend on the comments
  grep -v '^[[:space:]]*;@' lua_edge.asm > "$T/lua_plain.asm"
  "$BIN" --cart -B -o "$T/lp1.vbin" lua_edge.asm 2>/dev/null && "$BIN" --cart -B -o "$T/lp2.vbin" "$T/lua_plain.asm" 2>/dev/null && cmp -s "$T/lp1.vbin" "$T/lp2.vbin" && echo ok || echo "FAIL: ;@ lines change the program"
  # a nested function does not take over its parent's code; code after the last function has no function
  grep -q '^0x2000008B,edge.lua,13,f$' lua_edge.debug && grep -q '^0x20000141,edge.lua,20$' lua_edge.debug && echo ok || echo "FAIL: functions in lua_edge.debug"
) > "$T/varsout" 2>&1
grep '^FAIL' "$T/varsout"
pass=$((pass + $(grep -c '^ok' "$T/varsout")))
fail=$((fail + $(grep -c '^FAIL' "$T/varsout")))
# ;@lang, symbol locations in .vars, functions ended by the next function's label (SA-1, SA-2, SA-4)
( cd markers
  # SA-1/SA-4: a C-style source (three functions, nested blocks, no ";@src -" and no ";@fn" after the bodies)
  "$BIN" --cart -B -g -o "$T/cm.vbin" c_multi.asm 2>/dev/null && cmp -s "$T/cm.vbin.debug" c_multi.debug && cmp -s "$T/cm.vbin.vars" c_multi.vars \
    && echo ok || echo "FAIL: c_multi .debug / .vars"
  "$BIN" --cart -B -g -o "$T/cm.vbin" c_multi.asm 2>/dev/null; cmp -s "$T/cm.vbin.sym" c_multi.sym && echo ok || echo "FAIL: c_multi .sym (SA-3: resolved symbol file)"
  grep -q '^F,helper,0x2000000D,4,t.c,10$' c_multi.sym && grep -q '^V,counter,0x00000010,1,,$' c_multi.sym && echo ok || echo "FAIL: c_multi .sym lines"
  rm -f "$T/ss.vbin.sym"; "$BIN" --standard --cart -B -g -o "$T/ss.vbin" c_multi.asm 2>/dev/null; [ ! -e "$T/ss.vbin.sym" ] && echo ok || echo "FAIL: --standard writes no .sym"
  [ "$(head -1 c_multi.debug)" = "# lang c" ] && [ "$(head -1 c_multi.vars)" = "# lang c" ] && echo ok || echo "FAIL: # lang c is the first line of both files"
  # the function ends at the next function's label; a ;@fn above a label is only a hint; an unclosed block ends there too
  grep -q '^var,0x20000005,0x2000000D,argc,BP+2,int,-,0,main$' c_multi.vars && grep -q '^scope,0x2000000F,0x20000011,1,helper$' c_multi.vars \
    && grep -q '^0x2000000D,t.c,10,helper$' c_multi.debug && echo ok || echo "FAIL: c_multi ranges end at the next function label"
  # the same source under lua: the nested function keeps its parent open (unchanged), and "# lang lua" is the only difference
  ( echo ';@lang lua'; cat vars.asm ) > "$T/vl.asm"
  "$BIN" --cart -B -g -o "$T/vl.vbin" "$T/vl.asm" 2>/dev/null && ( echo '# lang lua'; cat vars.vars ) | cmp -s - "$T/vl.vbin.vars" \
    && ( echo '# lang lua'; cat vars.debug ) | cmp -s - "$T/vl.vbin.debug" && echo ok || echo "FAIL: ;@lang lua changes more than the # lang line"
  ( echo ';@lang Lua'; cat lua_dbg.asm ) > "$T/ld.asm"
  "$BIN" --cart -B -g -o "$T/ld.vbin" "$T/ld.asm" 2>/dev/null && ( echo '# lang Lua'; cat lua_dbg.vars ) | cmp -s - "$T/ld.vbin.vars" \
    && ( echo '# lang Lua'; cat lua_dbg.debug ) | cmp -s - "$T/ld.vbin.debug" && echo ok || echo "FAIL: ;@lang Lua (any case) is lua"
  # any other language ends a function at the next function label: vars.asm's nested function no longer sits inside outer
  ( echo ';@lang c'; cat vars.asm ) > "$T/vc.asm"
  "$BIN" --cart -B -g -o "$T/vc.vbin" "$T/vc.asm" 2>/dev/null && grep -q '^var,0x20000002,0x20000012,p,BP+2,lua,-,0,outer$' "$T/vc.vbin.vars" && echo ok || echo "FAIL: ;@lang c ends outer at the label of inner"
  # no ;@lang: no # line (the goldens of the earlier tests have none); a malformed or a second one
  grep -q '^#' vars.vars lua_dbg.vars lua_dbg.debug && echo "FAIL: # line without ;@lang" || echo ok
  ( echo ';@lang'; echo ';@lang c'; echo ';@lang lua'; cat c_multi.asm ) > "$T/lw.asm"
  "$BIN" --cart -B -g -Wmarkers -o "$T/lw.vbin" "$T/lw.asm" 2>"$T/lw.err"; [ "$(grep -c 'warning' "$T/lw.err")" = 2 ] && cmp -s "$T/lw.vbin.vars" c_multi.vars && echo ok || echo "FAIL: malformed and repeated ;@lang"
  # --standard: comments (no .vars, no # line); --map and plain -g: nothing changes
  rm -f "$T/sc.vbin.vars"; "$BIN" --standard --cart -B -g -o "$T/sc.vbin" c_multi.asm 2>/dev/null; [ ! -e "$T/sc.vbin.vars" ] && ! grep -q '^#' "$T/sc.vbin.debug" && echo ok || echo "FAIL: --standard and ;@lang"
  # (bench.asm has a ;@lang lua)
  "$BIN" --cart -B -g --map bench_mapwins.map -o "$T/bl.vbin" bench.asm 2>/dev/null && cmp -s "$T/bl.vbin.debug" bench_mapwins.debug && echo ok || echo "FAIL: --map and ;@lang"
  "$BIN" -C --map bench_mapwins.map -o "$T/bl.v32ob" bench.asm 2>/dev/null && "$LD" -c -g -o "$T/bl2.vbin" "$T/bl.v32ob" 2>/dev/null && cmp -s "$T/bl2.vbin.debug" bench_mapwins.debug && echo ok || echo "FAIL: -C --map and ;@lang"
  ( echo ';@lang c'; grep -v '^[[:space:]]*;@src' bench.asm ) > "$T/pl.asm"
  "$BIN" --cart -B -g -o "$T/pl.vbin" "$T/pl.asm" 2>/dev/null && ! grep -q '^#' "$T/pl.vbin.debug" && echo ok || echo "FAIL: plain -g and ;@lang"
  # ;@var lines without any ;@src: .vars has the line, the plain .debug has not
  grep -v '^[[:space:]]*;@src' c_multi.asm > "$T/ns.asm"
  "$BIN" --cart -B -g -o "$T/ns.vbin" "$T/ns.asm" 2>/dev/null && [ "$(head -1 "$T/ns.vbin.vars")" = "# lang c" ] && ! grep -q '^#' "$T/ns.vbin.debug" && echo ok || echo "FAIL: ;@lang with ;@var and no ;@src"
  "$BIN" -C -o "$T/ns.v32ob" "$T/ns.asm" 2>/dev/null && "$LD" -c -g -o "$T/ns2.vbin" "$T/ns.v32ob" 2>/dev/null && cmp -s "$T/ns2.vbin.vars" "$T/ns.vbin.vars" \
    && cmp -s "$T/ns2.vbin.debug" "$T/ns.vbin.debug" && echo ok || echo "FAIL: -C: ;@lang with ;@var and no ;@src"
  # a ;@var outside any function (a global) stays in scope to the end of the program: the first ";@src -" does not end it
  "$BIN" --cart -B -g -o "$T/cg.vbin" c_globals.asm 2>/dev/null && cmp -s "$T/cg.vbin.vars" c_globals.vars && echo ok || echo "FAIL: c_globals.vars (globals last to the end)"
  "$BIN" -C -o "$T/cg.v32ob" c_globals.asm 2>/dev/null && "$LD" -c -g -o "$T/cgl.vbin" "$T/cg.v32ob" 2>/dev/null && cmp -s "$T/cgl.vbin.vars" c_globals.vars && echo ok || echo "FAIL: c_globals.vars through the linker"
  # ;@home: a variable's home moves (v32opt promoting a slot to a register) and back, or is gone
  "$BIN" --cart -B -g -o "$T/ch.vbin" c_home.asm 2>/dev/null && cmp -s "$T/ch.vbin.vars" c_home.vars && echo ok || echo "FAIL: c_home.vars (;@home)"
  "$BIN" -C -o "$T/ch.v32ob" c_home.asm 2>/dev/null && "$LD" -c -g -o "$T/chl.vbin" "$T/ch.v32ob" 2>/dev/null && cmp -s "$T/chl.vbin.vars" c_home.vars && echo ok || echo "FAIL: c_home.vars through the linker"
  # SA-3 through the linker: the same F and R lines (an object has no %define lines, so no V lines)
  "$BIN" -C -o "$T/cm.v32ob" c_multi.asm 2>/dev/null && "$LD" -c -g -o "$T/cml.vbin" "$T/cm.v32ob" 2>/dev/null && [ "$(grep -v '^V,' c_multi.sym)" = "$(cat "$T/cml.vbin.sym")" ] && echo ok || echo "FAIL: v32asld -g .sym"
  # SA-2: symbol locations
  "$BIN" --cart -B -g -o "$T/cs.vbin" c_syms.asm 2>/dev/null && cmp -s "$T/cs.vbin.vars" c_syms.vars && echo ok || echo "FAIL: c_syms.vars"
  "$BIN" --cart -B -g vbin -o "$T/cs.vbin" c_syms.asm 2>/dev/null && cmp -s "$T/cs.vbin.vars" c_syms_vbin.vars && echo ok || echo "FAIL: c_syms.vars in -g vbin mode"
  [ "$("$BIN" --cart -B -g -Wmarkers -o "$T/cs.vbin" c_syms.asm 2>&1 | grep -c 'warning')" = 2 ] && echo ok || echo "FAIL: -Wmarkers on an unresolvable loc="
  [ "$("$BIN" --cart -B -g -o "$T/cs.vbin" c_syms.asm 2>&1 | grep -c 'warning')" = 0 ] && echo ok || echo "FAIL: an unresolvable loc= warns without -Wmarkers"
  [ "$("$BIN" --cart -B -g -Wmarkers -o "$T/cm.vbin" c_multi.asm 2>&1 | grep -c 'warning')" = 0 ] && echo ok || echo "FAIL: c_multi warns"
  grep -q '^var,.*,d,@0x00000078,' c_syms.vars && grep -q ',a,@0x20000007,' c_syms.vars && grep -q ',b,@0x20000009,' c_syms.vars \
    && grep -q ',j,BP-3,' c_syms.vars && grep -q ',k,R5,' c_syms.vars && grep -q ',l,@0x20003000,' c_syms.vars && grep -q ',m,-,' c_syms.vars \
    && grep -q ',n,_nowhere,' c_syms.vars && echo ok || echo "FAIL: c_syms.vars content"
  "$BIN" --standard --cart -B -g -o "$T/sc.vbin" c_syms.asm 2>/dev/null; [ ! -e "$T/sc.vbin.vars" ] && echo ok || echo "FAIL: --standard writes .vars (c_syms)"
  # -C: the objects carry the text (a %define'd number is resolved by the assembler, a label by the linker); the result is the same file
  for m in program vbin v32; do
    for n in c_multi c_syms; do
      "$BIN" --cart -B -g $m -o "$T/e1.vbin" $n.asm 2>/dev/null; "$BIN" -C -o "$T/e.v32ob" $n.asm 2>/dev/null \
        && "$LD" -c -g $m -o "$T/e2.vbin" "$T/e.v32ob" 2>/dev/null && cmp -s "$T/e1.vbin.vars" "$T/e2.vbin.vars" && cmp -s "$T/e1.vbin.debug" "$T/e2.vbin.debug" \
        && "$LD" -R -o "$T/er.v32ob" "$T/e.v32ob" 2>/dev/null && "$LD" -c -g $m -o "$T/e3.vbin" "$T/er.v32ob" 2>/dev/null \
        && cmp -s "$T/e1.vbin.vars" "$T/e3.vbin.vars" && cmp -s "$T/e1.vbin.debug" "$T/e3.vbin.debug" && echo ok || echo "FAIL: $n -C + v32asld -g $m equals v32as -g $m"
    done
  done
  "$BIN" -C -o "$T/e.v32ob" c_multi.asm 2>/dev/null; [ "$(od -An -tx4 -j8 -N4 "$T/e.v32ob" | tr -d ' ')" = 00000004 ] && echo ok || echo "FAIL: object version 4 with ;@lang"
  # two C files: a label of the other file, a local label, a %define: linking equals the joined source, also partially linked
  cat c_a.asm c_b.asm > "$T/ab.asm"
  "$BIN" --cart -B -g -o "$T/ab.vbin" "$T/ab.asm" 2>/dev/null && cmp -s "$T/ab.vbin.vars" c_ab.vars && cmp -s "$T/ab.vbin.debug" c_ab.debug && echo ok || echo "FAIL: c_ab goldens"
  "$BIN" -C -o "$T/ca.v32ob" c_a.asm 2>/dev/null && "$BIN" -C -o "$T/cb.v32ob" c_b.asm 2>/dev/null && "$LD" -c -g -o "$T/ab2.vbin" "$T/ca.v32ob" "$T/cb.v32ob" 2>/dev/null \
    && cmp -s "$T/ab2.vbin.vars" c_ab.vars && cmp -s "$T/ab2.vbin.debug" c_ab.debug && echo ok || echo "FAIL: c_a + c_b linked == joined"
  "$LD" -R -o "$T/cab.v32ob" "$T/ca.v32ob" "$T/cb.v32ob" 2>/dev/null && "$LD" -c -g -o "$T/ab3.vbin" "$T/cab.v32ob" 2>/dev/null \
    && cmp -s "$T/ab3.vbin.vars" c_ab.vars && cmp -s "$T/ab3.vbin.debug" c_ab.debug && echo ok || echo "FAIL: c_a + c_b partially linked"
  "$LD" -c -g -o "$T/ab4.vbin" "$T/cb.v32ob" "$T/ca.v32ob" 2>/dev/null; [ "$(head -1 "$T/ab4.vbin.vars")" = "# lang c" ] && echo ok || echo "FAIL: link order and # lang"
  # objects of the earlier versions (made by the previous release) are still read; a version 3 object's label locs are resolved too
  # (the objects are kept as octal escapes of their bytes: printf "$(cat F.oct)" rebuilds them)
  for f in old_v3_1 old_v3_2 old_v2; do printf "$(cat $f.oct)" > "$T/$f.v32ob"; done
  "$LD" -c -g -o "$T/ov.vbin" "$T/old_v3_1.v32ob" "$T/old_v3_2.v32ob" 2>/dev/null && cmp -s "$T/ov.vbin.vars" vl12.vars && cmp -s "$T/ov.vbin.debug" vl12.debug && echo ok || echo "FAIL: version 3 objects"
  "$LD" -c -g -o "$T/ov2.vbin" "$T/old_v2.v32ob" 2>/dev/null && ! grep -q '^#' "$T/ov2.vbin.debug" && echo ok || echo "FAIL: version 2 object"
  # an object without ;@lang is not a version 4 one
  [ "$(od -An -tx4 -j8 -N4 "$T/old_v3_1.v32ob" | tr -d ' ')" = 00000003 ] && "$BIN" -C -o "$T/nl.v32ob" vl1.asm 2>/dev/null && [ "$(od -An -tx4 -j8 -N4 "$T/nl.v32ob" | tr -d ' ')" = 00000003 ] && cmp -s "$T/nl.v32ob" "$T/old_v3_1.v32ob" && echo ok || echo "FAIL: no ;@lang, same object as before"
) > "$T/langout" 2>&1
grep '^FAIL' "$T/langout"
pass=$((pass + $(grep -c '^ok' "$T/langout")))
fail=$((fail + $(grep -c '^FAIL' "$T/langout")))
# file-local labels: their names are kept only for -g and --symbols; the output is the same either way (golden from before that change)
( cd locals
  "$BIN" --cart -B -o "$T/l.vbin" many.asm 2>/dev/null && od -An -tx4 -v -w4 "$T/l.vbin" | cmp -s - many.hex && echo ok || echo "FAIL: locals without -g"
  "$BIN" --cart -B -g -o "$T/lg.vbin" many.asm 2>/dev/null && od -An -tx4 -v -w4 "$T/lg.vbin" | cmp -s - many.hex && echo ok || echo "FAIL: locals -g (vbin)"
  cmp -s "$T/lg.vbin.sym" many.sym && echo ok || echo "FAIL: locals -g (.sym)"
  cmp -s "$T/lg.vbin.debug" many.debug && echo ok || echo "FAIL: locals -g (.debug)"
  "$BIN" --cart -B --symbols "$T/ls.sym" -o "$T/l2.vbin" many.asm 2>/dev/null && cmp -s "$T/ls.sym" many_nolocal.sym && cmp -s "$T/l2.vbin" "$T/l.vbin" && echo ok || echo "FAIL: locals --symbols"
  "$BIN" --cart -B --symbols "$T/la.sym" --symbols-all -o "$T/l3.vbin" many.asm 2>/dev/null && cmp -s "$T/la.sym" many_all.sym && cmp -s "$T/l3.vbin" "$T/l.vbin" && echo ok || echo "FAIL: locals --symbols-all"
  "$BIN" --cart -B --stats -o "$T/l4.vbin" many.asm 2>&1 | grep -q 'heap' && cmp -s "$T/l4.vbin" "$T/l.vbin" && echo ok || echo "FAIL: --stats"
) > "$T/locout" 2>&1
grep '^FAIL' "$T/locout"
pass=$((pass + $(grep -c '^ok' "$T/locout")))
fail=$((fail + $(grep -c '^FAIL' "$T/locout")))
# the interactive mode is -i (here fed from a pipe); it takes no files and no output options
(
  out=$(printf '_a: mov R0, 5\njmp _a\n.dump\n.quit\n' | "$BIN" -i -c 2>&1)
  echo "$out" | grep -q '^0x20000003  0x20000000$' && echo ok || echo "FAIL: -i assembles and .dump lists the words"
  out=$(printf 'mov R0, 1\n' | "$BIN" --interactive 2>&1)
  echo "$out" | grep -q 'interactive mode' && echo ok || echo "FAIL: --interactive starts the interactive mode"
  for o in "x.asm" "-o x.vbin" "-E" "-B" "-C" "-g" "-s"; do
    "$BIN" -i $o </dev/null >/dev/null 2>&1; [ $? -eq 2 ] && echo ok || echo "FAIL: -i $o is a usage error"
  done
  # an output file that cannot be written is status 2 (it used to crash in fclose (NULL))
  printf 'mov R0, 1\n' > "$T/w.asm"
  "$BIN" -o "$T/no/such/dir/w.vbin" "$T/w.asm" >/dev/null 2>&1; [ $? -eq 2 ] && echo ok || echo "FAIL: unwritable output is status 2"
  # release audit: a macro that %unmacros itself while running (was a use-after-free)
  printf '%%macro M 0\nx\n%%unmacro M\ny\n%%endmacro\nM\nM\n' > "$T/u.asm"
  [ "$("$BIN" -E -P "$T/u.asm" 2>&1 | tr '\n' ' ')" = "x y M " ] && echo ok || echo "FAIL: self-%unmacro"
  # a recursive macro: a handful of errors, not one per leaf of the expansion tree
  printf '%%macro ANY 0\n ANY\n ANY\n%%endmacro\nANY\n' > "$T/r.asm"
  n=$("$BIN" -E -P "$T/r.asm" 2>&1 >/dev/null | wc -l); [ "$n" -lt 100 ] && echo ok || echo "FAIL: recursive macro floods ($n lines)"
  # a deeply nested expression is an error, not a stack overflow
  awk 'BEGIN { s = "%assign X "; for (i = 0; i < 5000; i++) s = s "("; s = s "1"; for (i = 0; i < 5000; i++) s = s ")"; print s }' > "$T/e.asm"
  "$BIN" -E -P "$T/e.asm" 2>&1 | grep -q 'nested too deeply' && echo ok || echo "FAIL: deep expression"
  # a directory is not an input file, an include, or a datafile
  "$BIN" "$T" >/dev/null 2>&1; [ $? -eq 1 ] && echo ok || echo "FAIL: directory as input is status 1"
  printf '%%include "."\n' > "$T/d.asm"; "$BIN" -E "$T/d.asm" >/dev/null 2>&1; [ $? -eq 1 ] && echo ok || echo "FAIL: %%include of a directory"
  # -o naming an input file is refused, and the source survives
  cp "$T/w.asm" "$T/w2.asm"; "$BIN" -E "$T/w2.asm" -o "$T/w2.asm" >/dev/null 2>&1
  [ $? -eq 2 ] && [ -s "$T/w2.asm" ] && echo ok || echo "FAIL: -o onto an input file"
  # --standard: stock accepts a trailing comma in data lists, refuses a bare port value outside OUT
  printf 'integer 1,\nfloat 1,2,\n' > "$T/tc.asm"; "$BIN" --standard "$T/tc.asm" -o "$T/tc.vbin" 2>/dev/null && echo ok || echo "FAIL: trailing comma"
  printf 'mov R0, GPUCommand_ClearScreen\n' > "$T/pv.asm"
  "$BIN" --standard "$T/pv.asm" -o "$T/pv.vbin" >/dev/null 2>&1; [ $? -eq 1 ] && echo ok || echo "FAIL: --standard port value in mov"
  "$BIN" "$T/pv.asm" -o "$T/pv.vbin" 2>/dev/null && echo ok || echo "FAIL: extended mode keeps port values as numbers"
  # without -i, a pipe on STDIN is still assembled to STDOUT
  printf 'mov R0, 1\n' | "$BIN" | od -An -tx1 | head -1 | grep -q '56 33 32 2d 56 42 49 4e' && echo ok || echo "FAIL: STDIN to STDOUT without -i"
) > "$T/replout" 2>&1
grep '^FAIL' "$T/replout"
pass=$((pass + $(grep -c '^ok' "$T/replout")))
fail=$((fail + $(grep -c '^FAIL' "$T/replout")))
echo "passed $pass, failed $fail"
[ $fail -eq 0 ]
