#!/bin/sh
# usage: run_tests.sh path/to/v32asc   (needs only sh, od, cmp, grep)
BIN=${1:-../bin/v32asc}
cd "$(dirname "$0")" || exit 1
case $BIN in /*) ;; *) BIN=$OLDPWD/$BIN ;; esac
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
echo "passed $pass, failed $fail"
[ $fail -eq 0 ]
