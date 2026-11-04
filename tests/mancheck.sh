#!/bin/sh
# usage: mancheck.sh FILE...
# A small lint for man(7) pages (there is no groff requirement): the page has a .TH line near
# the top whose name and section match the file name, the sections NAME (first), SYNOPSIS
# (sections 1-3), DESCRIPTION and SEE ALSO, balanced .nf/.fi, .EX/.EE and .RS/.RE, no line
# starting with an apostrophe, and nothing but ASCII.
rc=0
[ $# -gt 0 ] || { echo "usage: mancheck.sh FILE..." >&2; exit 2; }
for f in "$@"; do
    out=$(LC_ALL=C awk -v file="$f" '
        function bad(n, msg) { printf("  %s:%d: %s\n", file, n, msg); errs++ }
        BEGIN {
            base = file; sub(/.*\//, "", base)
            sect = base; sub(/.*\./, "", sect)
            name = base; sub(/\.[^.]*$/, "", name)
        }
        /[^\t -~]/ { bad(NR, "non-ASCII or control character") }
        /^'"'"'/ { bad(NR, "line starts with an apostrophe") }
        /^\.TH / {
            hasth = 1
            if (NR > 10) bad(NR, ".TH is not near the top")
            n = $2; gsub(/"/, "", n)
            s = $3; gsub(/"/, "", s)
            if (tolower(n) != tolower(name)) bad(NR, ".TH name " n " does not match the file name")
            if (s != sect) bad(NR, ".TH section " s " does not match the file name")
        }
        /^\.SH / {
            t = $0; sub(/^\.SH +/, "", t); gsub(/"/, "", t); t = toupper(t)
            nsec++
            if (nsec == 1 && t != "NAME") bad(NR, "the first section must be NAME")
            seen[t] = 1
        }
        /^\.(nf|EX)([ ]|$)/ { nf++ }
        /^\.(fi|EE)([ ]|$)/ { nf--; if (nf < 0) { bad(NR, ".fi/.EE without .nf/.EX"); nf = 0 } }
        /^\.RS/ { rs++ }
        /^\.RE/ { rs--; if (rs < 0) { bad(NR, ".RE without .RS"); rs = 0 } }
        END {
            if (!hasth) bad(0, "no .TH line")
            if (!seen["DESCRIPTION"]) bad(0, "no DESCRIPTION section")
            if (!seen["SEE ALSO"]) bad(0, "no SEE ALSO section")
            if (sect + 0 >= 1 && sect + 0 <= 3 && !seen["SYNOPSIS"]) bad(0, "no SYNOPSIS section")
            if (nf != 0) bad(0, "unbalanced .nf/.fi (or .EX/.EE)")
            if (rs != 0) bad(0, "unbalanced .RS/.RE")
            exit(errs > 0)
        }' "$f") || { echo "FAIL $f"; echo "$out"; rc=1; continue; }
    echo "ok   $f"
done
exit $rc
