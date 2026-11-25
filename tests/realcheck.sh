#!/bin/sh
# ----------------------------------------------------------------------------
#   realcheck: v32as against the official Vircon32 assembler
#
#   usage: tests/realcheck.sh [--combo] [--keep] STOCK V32AS
#          (normally through `make realcheck`, `make realcheck COMBO=1`)
#
#   STOCK is a checkout of the official ComputerSoftware repository (the
#   folder that holds DevelopmentTools/ and VirconDefinitions/). Its
#   assembler (`assemble`) and C compiler (`compile`) are built from it with
#   $CXX, with a two-line SDL stand-in (they only ask SDL for their own
#   folder), unless ASSEMBLE= and COMPILE= name tools already built. The
#   source lists are read from DevelopmentTools/CMakeLists.txt, so a newer
#   checkout builds the same way.
#
#   Every program of these corpora is assembled by the official tool, by
#   `v32as --standard` and by plain `v32as`:
#     stock-asm   DevelopmentTools/Assembler/Tests/*.asm (cartridge and BIOS)
#     stock-c     DevelopmentTools/CCompiler/Tests/*.asm, as shipped
#     stock-cc    DevelopmentTools/CCompiler/Tests/*.c, compiled now by the
#                 official compiler (the ones it accepts)
#     stock-rom   DevelopmentTools/RomPacker/Tests/*.asm
#     v32as       this project's own test sources (tests/**/*.asm): many use
#                 extensions, so mostly the acceptance is compared
#     combo       (--combo) every opcode with none, one and two operands from
#                 a pool of operand forms: about 52,000 one-line programs
#
#   A program the official tool assembles must give the same VBIN, byte for
#   byte, under --standard and (barring a leading-zero literal, which is
#   octal in extended mode and warned about) without it; its -g files must
#   be the same too (`-g program` exactly; `-g vbin` / `-g v32` once the
#   official tool's wrapped-around offsets are unwrapped, see README). A
#   program the official tool refuses should be refused by --standard: when
#   it is not, that is listed as a leniency (not a failure: v32as --standard
#   accepts a few things the official tool refuses, see README).
#   tests/realcheck.known lists the cases where a difference is deliberate.
#
#   Results: one line per check in WORK/report.txt (WORK defaults to
#   build/realcheck); failures and a summary on the terminal. Exit status
#   0 if nothing failed, 1 otherwise, 2 for a usage or build problem.
#
#   Environment: CXX (default c++), CXXFLAGS (default -O1), JOBS (parallel
#   workers for --combo, default 4), WORK, ASSEMBLE, COMPILE.
# ----------------------------------------------------------------------------

combo=0
keep=0
while [ $# -gt 0 ]; do
    case $1 in
        --combo) combo=1; shift ;;
        --keep)  keep=1; shift ;;
        --one-combo) shift; mode=one-combo; break ;;
        -*) echo "realcheck: unknown option $1" >&2; exit 2 ;;
        *) break ;;
    esac
done

abspath() { case $1 in /*) printf '%s\n' "$1" ;; *) printf '%s/%s\n' "$(pwd)" "$1" ;; esac; }

# ---------------------------------------------------------------- one check
# check CORPUS NAME DIR FILE [STOCK-OPTION]: assemble DIR/FILE (from DIR, as
# the official tool resolves includes and data files from the current
# directory) with the three assemblers and record the outcome
check() {
    corpus=$1 name=$2 dir=$3 file=$4 opt=${5:-}
    o=$OUT/$$
    rm -f "$o".s.vbin "$o".v.vbin "$o".x.vbin
    ( cd "$dir" && "$ASSEMBLE" $opt "$file" -o "$o.s.vbin" ) >"$o.s.err" 2>&1; rs=$?
    [ $rs = 0 ] && [ -f "$o.s.vbin" ] || rs=1
    ( cd "$dir" && "$V32AS" --standard $opt "$file" -o "$o.v.vbin" ) >"$o.v.err" 2>&1; rv=$?
    ( cd "$dir" && "$V32AS" $opt "$file" -o "$o.x.vbin" ) >"$o.x.err" 2>&1; rx=$?
    id="$corpus $name${opt:+ $opt}"

    if [ $rs = 0 ]; then
        if [ $rv != 0 ]; then
            verdict FAIL "$id" "the official tool assembles it, --standard refuses: $(head -1 "$o.v.err")"
        elif ! cmp -s "$o.s.vbin" "$o.v.vbin"; then
            verdict FAIL "$id" "--standard: the VBIN differs ($(cmp "$o.s.vbin" "$o.v.vbin" 2>&1 | sed 's/.*differ: //'))"
        else
            verdict ok "$id" "--standard: same VBIN"
            [ "$corpus" != v32as ] && check_debug "$id" "$dir" "$file" "$opt"
        fi
        if [ $rx != 0 ]; then
            verdict FAIL "$id" "the official tool assembles it, v32as (extended) refuses: $(head -1 "$o.x.err")"
        elif ! cmp -s "$o.s.vbin" "$o.x.vbin"; then
            if grep -q 'leading zero' "$o.x.err"; then
                verdict octal "$id" "extended mode: a leading-zero literal is octal (warned)"
            else
                verdict FAIL "$id" "v32as (extended): the VBIN differs"
            fi
        else
            verdict ok "$id" "extended: same VBIN"
        fi
    elif [ $rv = 0 ]; then
        verdict lenient "$id" "the official tool refuses ($(grep -v 'assembly terminated' "$o.s.err" | head -1)), --standard accepts"
    else
        verdict ok "$id" "both refuse"
    fi
}

# check_debug ID DIR FILE OPT: the -g files, for the three reference modes
check_debug() {
    for m in program vbin v32; do
        rm -f "$o".s.vbin.debug "$o".v.vbin.debug
        ( cd "$2" && "$ASSEMBLE" $4 "$3" -o "$o.s.vbin" -g $m ) >/dev/null 2>&1
        ( cd "$2" && "$V32AS" --standard $4 "$3" -o "$o.v.vbin" -g $m ) >/dev/null 2>&1
        [ -f "$o.s.vbin.debug" ] || { verdict FAIL "$1" "-g $m: the official tool wrote no .debug"; continue; }
        if [ $m = program ]; then
            cmp -s "$o.s.vbin.debug" "$o.v.vbin.debug" \
                && verdict ok "$1" "-g $m: same .debug" || verdict FAIL "$1" "-g $m: the .debug differs"
        else
            # the official tool computes these offsets as 4 * address + header, without taking the
            # base address off, so they wrap around 32 bits: subtract 4 * base (mod 2^32) again
            case "$4" in -b) wrap=1073741824 ;; *) wrap=2147483648 ;; esac      # 4 * 0x10000000, 4 * 0x20000000
            awk -F, -v OFS=, -v wrap="$wrap" '
                function hex(s,   i, n) { n = 0; s = toupper(substr(s, 3))
                                          for (i = 1; i <= length(s); i++) n = n * 16 + index("0123456789ABCDEF", substr(s, i, 1)) - 1
                                          return n }
                { v = hex($1) - wrap; if (v < 0) v += 4294967296; $1 = sprintf("0x%08X", v); print }' \
                "$o.s.vbin.debug" > "$o.s.masked"
            cmp -s "$o.s.masked" "$o.v.vbin.debug" \
                && verdict ok "$1" "-g $m: same .debug (once unwrapped)" || verdict FAIL "$1" "-g $m: the .debug differs"
        fi
    done
}

verdict() { printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$REPORT"; }

# ------------------------------------------------------ a worker for --combo
# (runs a chunk of one-line programs; writes its own report lines)
if [ "${mode:-}" = one-combo ]; then
    chunk=$1; ASSEMBLE=$2; V32AS=$3; OUT=$4; REPORT=$5; KNOWN=/dev/null
    d=$OUT/combo.$$
    mkdir -p "$d"
    while IFS= read -r insn; do
        printf '%s\n_a:\n' "$insn" > "$d/t.asm"
        rm -f "$d/s.vbin" "$d/v.vbin"
        ( cd "$d" && "$ASSEMBLE" t.asm -o s.vbin ) >"$d/s.err" 2>&1; rs=$?
        [ $rs = 0 ] && [ -f "$d/s.vbin" ] || rs=1
        ( cd "$d" && "$V32AS" --standard t.asm -o v.vbin ) >"$d/v.err" 2>&1; rv=$?
        if [ $rs = 0 ] && [ $rv = 0 ]; then
            cmp -s "$d/s.vbin" "$d/v.vbin" && verdict ok "combo $insn" "same VBIN" || verdict FAIL "combo $insn" "--standard: the VBIN differs"
        elif [ $rs = 0 ]; then
            verdict FAIL "combo $insn" "the official tool assembles it, --standard refuses: $(head -1 "$d/v.err")"
        elif [ $rv = 0 ]; then
            verdict lenient "combo $insn" "the official tool refuses ($(grep -v 'assembly terminated' "$d/s.err" | head -1)), --standard accepts"
        else
            verdict ok "combo $insn" "both refuse"
        fi
    done < "$chunk"
    rm -rf "$d"
    exit 0
fi

# ------------------------------------------------------------------- set up
if [ $# -ne 2 ]; then
    echo "usage: $0 [--combo] [--keep] STOCK V32AS" >&2
    echo "  STOCK: a checkout of the official ComputerSoftware repository" >&2
    exit 2
fi
STOCK=$(abspath "$1")
V32AS=$(abspath "$2")
HERE=$(cd "$(dirname "$0")" && pwd)
DEV=$STOCK/DevelopmentTools
KNOWN=$HERE/realcheck.known
WORK=$(abspath "${WORK:-build/realcheck}")
CXX=${CXX:-c++}
CXXFLAGS=${CXXFLAGS:--O1}
JOBS=${JOBS:-4}

[ -f "$DEV/CMakeLists.txt" ] && [ -d "$STOCK/VirconDefinitions" ] || {
    echo "realcheck: '$1' is not a ComputerSoftware checkout (no DevelopmentTools/CMakeLists.txt)" >&2; exit 2; }
[ -x "$V32AS" ] || { echo "realcheck: no v32as at '$2' (run make first)" >&2; exit 2; }

mkdir -p "$WORK/stock/sdl" "$WORK/out"
OUT=$WORK/out
REPORT=$WORK/report.txt
: > "$REPORT"

# the files of a set(NAME ...) list in DevelopmentTools/CMakeLists.txt
srclist() {
    awk -v v="$1" '
        $0 ~ "^set\\(" v "([ \t]|$)" { on = 1; sub("^set\\(" v, "") }
        on { line = $0; done = (line ~ /\)/); gsub(/\)/, "", line)
             n = split(line, w, /[ \t]+/)
             for (i = 1; i <= n; i++) if (w[i] != "") print w[i]
             if (done) exit }' "$DEV/CMakeLists.txt" |
    sed -e 's|\${C_COMPILER_DIR}/*|CCompiler/|' -e 's|\${ASSEMBLER_DIR}/*|Assembler/|' \
        -e 's|\${INFRASTRUCTURE_DIR}/*|DevToolsInfrastructure/|' |
    while IFS= read -r f; do printf '%s/%s\n' "$DEV" "$f"; done
}

# build TOOL LIST: the official tool, unless it is newer than its sources
build() {
    bin=$WORK/stock/$1
    srcs=$(srclist "$2")
    [ -n "$srcs" ] || { echo "realcheck: no $2 list in $DEV/CMakeLists.txt" >&2; exit 2; }
    if [ -x "$bin" ] && [ -z "$(find $srcs "$DEV/CMakeLists.txt" -newer "$bin" 2>/dev/null)" ]; then
        return 0
    fi
    echo "realcheck: building the official '$1' with $CXX"
    # shellcheck disable=SC2086
    $CXX -std=c++17 $CXXFLAGS -w -I"$WORK/stock/sdl" -I"$DEV/DevToolsInfrastructure" -I"$STOCK/VirconDefinitions" \
        -DREALCHECK_BASE="\"$WORK/stock/\"" $srcs -o "$bin" || { echo "realcheck: building '$1' failed" >&2; exit 2; }
}

if [ -z "${ASSEMBLE:-}" ] || [ -z "${COMPILE:-}" ]; then
    # the tools only ask SDL for the folder they are in (the compiler finds
    # its include/ there)
    cat > "$WORK/stock/sdl/SDL.h" <<'EOF'
#include <stdlib.h>
#include <string.h>
#define SDL_Init(flags)    0
#define SDL_GetBasePath()  strdup (REALCHECK_BASE)
#define SDL_free           free
#define SDL_Quit()
EOF
    [ -n "${ASSEMBLE:-}" ] || { build assemble ASSEMBLER_SRC; ASSEMBLE=$WORK/stock/assemble; }
    [ -n "${COMPILE:-}" ]  || { build compile C_COMPILER_SRC; COMPILE=$WORK/stock/compile; }
    rm -rf "$WORK/stock/include"
    cp -R "$DEV/Data/include" "$WORK/stock/include"
fi
ASSEMBLE=$(abspath "$ASSEMBLE")
COMPILE=$(abspath "$COMPILE")
echo "realcheck: $("$V32AS" --version) against the official tools in $STOCK"

# ------------------------------------------------------------------ corpora
for f in "$DEV"/Assembler/Tests/*.asm; do
    [ -f "$f" ] || continue
    check stock-asm "${f##*/}" "$DEV/Assembler/Tests" "${f##*/}"
    check stock-asm "${f##*/}" "$DEV/Assembler/Tests" "${f##*/}" -b
done

for f in "$DEV"/CCompiler/Tests/*.asm; do
    [ -f "$f" ] && check stock-c "${f##*/}" "$DEV/CCompiler/Tests" "${f##*/}"
done

# the official compiler's output, made now (in a copy of the folder, with
# the headers and data files the tests include)
cc=$WORK/cc
rm -rf "$cc"
mkdir -p "$cc"
cp -R "$DEV/CCompiler/Tests/." "$cc/"
rm -f "$cc"/*.asm
ncc=0
for f in "$cc"/*.c; do
    [ -f "$f" ] || continue
    b=${f##*/}; b=${b%.c}
    if ( cd "$cc" && "$COMPILE" "$b.c" -o "$b.asm" ) >/dev/null 2>&1; then
        check stock-cc "$b.c" "$cc" "$b.asm"
        ncc=$((ncc + 1))
    else
        verdict skip "stock-cc $b.c" "the official compiler refuses it"
    fi
done

for f in "$DEV"/RomPacker/Tests/*.asm; do
    [ -f "$f" ] && check stock-rom "${f##*/}" "$DEV/RomPacker/Tests" "${f##*/}"
done

find "$HERE" -name '*.asm' -not -path "$HERE/rom/*" | sort | while IFS= read -r f; do
    check v32as "${f#"$HERE"/}" "$(dirname "$f")" "${f##*/}"
done

# --------------------------------------------------- optional: combinations
if [ $combo = 1 ]; then
    ops=$(sed -n '/OpCodeNames[^=]*=/,/};/p' "$DEV/DevToolsInfrastructure/EnumStringConversions.cpp" |
          grep -o '"[A-Za-z]*"' | tr -d '"')
    pool='R1 SP 5 -5 0 1.5 -1.5 _a [R2] [R2+4] [R2-4] [R2+0] [5] [_a] [-5] GPU_Command GPUCommand_ClearScreen
          true 0x1FFFF [SP] [0x7FFFFFFF] -0 -0.0 +3 TIM_CurrentDate 300 [R2+1.5] [GPU_Command]'
    list=$WORK/combo.list
    : > "$list"
    for op in $ops; do
        echo "$op" >> "$list"
        for x in $pool; do
            echo "$op $x" >> "$list"
            for y in $pool; do echo "$op $x, $y" >> "$list"; done
        done
    done
    total=$(wc -l < "$list")
    echo "realcheck: $total one-line programs, $JOBS at a time"
    rm -f "$WORK"/combo.part.* "$WORK"/combo.report.*
    lines=$(( (total + JOBS - 1) / JOBS ))
    split -l "$lines" "$list" "$WORK/combo.part."
    for part in "$WORK"/combo.part.*; do
        sh "$0" --one-combo "$part" "$ASSEMBLE" "$V32AS" "$OUT" "$part.report" &
    done
    wait
    cat "$WORK"/combo.part.*.report >> "$REPORT"
    rm -f "$WORK"/combo.part.*
fi

# ------------------------------------------------------------------ results
# a failure listed in realcheck.known is expected (and so is its absence)
awk -F '\t' -v known="$KNOWN" '
    BEGIN { while ((getline l < known) > 0) if (l !~ /^#/ && l != "") { split(l, k, "\t"); why[k[1]] = k[2] } }
    { key = $2; sub(/ -b$/, "", key) }
    $1 == "FAIL" && (key in why) { $1 = "known"; $3 = $3 " [" why[key] "]" }
    { print $1 "\t" $2 "\t" $3 }' OFS='\t' "$REPORT" > "$REPORT.tmp" && mv "$REPORT.tmp" "$REPORT"

awk -F '\t' '$1 == "FAIL" { print "FAIL: " $2 ": " $3 }' "$REPORT"
for v in ok FAIL lenient known octal skip; do
    eval "n_$v=\$(grep -c \"^$v	\" \"\$REPORT\")"
done
echo "realcheck: $n_ok agree, $n_FAIL FAIL, $n_lenient leniencies (the official tool refuses, --standard accepts)," \
     "$n_known known differences, $n_octal octal literals in extended mode, $n_skip C tests the official compiler refuses" \
     "($ncc compiled); details in $REPORT"
[ $keep = 1 ] || rm -rf "$OUT" "$cc"
[ "$n_FAIL" = 0 ]
