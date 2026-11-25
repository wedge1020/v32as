#!/bin/sh
# ----------------------------------------------------------------------------
#   Release smoke test for built v32as / v32asld binaries
#
#   usage: scripts/smoke_test.sh <bin dir> <library dir> [<version>]
#
#   Lays the binaries out the way the release zips (and Windows installs) do
#   -- v32as and v32asld next to include/v32as/*.inc -- in a scratch
#   directory, then, from an unrelated directory:
#     1. `v32as --version` and `v32asld --version` must run (and print
#        <version>, when given);
#     2. a program that %includes every library file must assemble with no
#        -I, so the library can only have been found next to the assembler;
#     3. the same program assembled as an object and linked with v32asld
#        must give the same VBIN;
#     4. `v32as -i` must read and assemble lines (from a pipe here).
#
#   Plain POSIX sh: runs on Linux, macOS, MSYS2, OpenBSD (ksh) and Haiku.
#   Used by .github/workflows/release-builds.yml; harmless to run by hand:
#       scripts/smoke_test.sh bin lib
# ----------------------------------------------------------------------------
set -eu

if [ $# -lt 2 ]; then
    echo "usage: $0 <bin dir> <library dir> [<version>]" >&2
    exit 2
fi
bindir=$1
lib=$2
want=${3:-}

fail() {
    echo "smoke test FAILED: $*" >&2
    exit 1
}

if [ -f "$bindir/v32as.exe" ]; then ext=.exe; else ext=; fi
[ -f "$bindir/v32as$ext" ]   || fail "no v32as$ext in $bindir"
[ -f "$bindir/v32asld$ext" ] || fail "no v32asld$ext in $bindir"
ls "$lib"/*.inc >/dev/null 2>&1 || fail "no *.inc files in $lib"

tmp=$(mktemp -d 2>/dev/null || mktemp -d -t v32as-smoke)
trap 'rm -rf "$tmp"' EXIT INT TERM

mkdir -p "$tmp/pkg/include/v32as" "$tmp/work"
cp "$bindir/v32as$ext" "$bindir/v32asld$ext" "$tmp/pkg/"
cp "$lib"/*.inc "$tmp/pkg/include/v32as/"
as="$tmp/pkg/v32as$ext"
ld="$tmp/pkg/v32asld$ext"

# 1. The binaries run and report the expected version.
for tool in "$as" "$ld"; do
    out=$("$tool" --version) || fail "'$tool --version' did not run"
    echo "$out"
    if [ -n "$want" ]; then
        case "$out" in
            *"$want"*) ;;
            *) fail "--version printed '$out', expected '$want'" ;;
        esac
    fi
done

# 2. Every library file is found through %include, next to the assembler.
{
    for f in "$lib"/*.inc; do
        printf '%%include <%s>\n' "${f##*/}"
    done
    printf '_main:\n    mov R0, 1\n    hlt\n'
} > "$tmp/work/smoke.asm"

cd "$tmp/work"
unset V32AS_BUILD_SCRIPT 2>/dev/null || true
"$as" smoke.asm || fail "smoke.asm did not assemble (library not found beside v32as?)"
[ -s smoke.vbin ] || fail "no smoke.vbin written"

# 3. Assemble to an object and link it: the same VBIN.
"$as" -C smoke.asm            || fail "v32as -C failed"
"$ld" -c -o linked.vbin smoke.v32ob || fail "v32asld failed"
cmp -s smoke.vbin linked.vbin || fail "linked VBIN differs from the directly assembled one"

# 4. The interactive mode, fed from a pipe.
out=$(printf 'mov R0, 5\n.dump\n.quit\n' | "$as" -i -c) || fail "v32as -i failed"
case "$out" in
    *0x20000001*0x00000005*) ;;
    *) fail "v32as -i did not assemble its input: $out" ;;
esac

echo "smoke test passed"
