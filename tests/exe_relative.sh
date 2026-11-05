#!/bin/sh
# usage: exe_relative.sh path/to/v32as  -- %include "v32as.inc" is found beside the program, with no -I
BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); LIB=$(cd "$(dirname "$0")/../lib" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/share/v32as/lib"; cp "$BIN" "$T/bin/v32as"; cp "$LIB"/*.inc "$T/share/v32as/lib/"
printf '%%include "v32as.inc"\nmov R0, 1\nhlt\n' > "$T/t.asm"
cd "$T" && "$T/bin/v32as" -o "$T/t.vbin" "$T/t.asm" >"$T/log" 2>&1 && [ -s "$T/t.vbin" ] && echo "ok   exe_relative" || { cat "$T/log"; echo "FAIL exe_relative"; exit 1; }
