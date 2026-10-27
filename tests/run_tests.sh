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
echo "passed $pass, failed $fail"
[ $fail -eq 0 ]
