#!/bin/sh
# Property: mdcat distributes over its file-argument list.
#
#   mdcat f1 f2 ... fn   ==   (mdcat f1)(mdcat f2)...(mdcat fn)
#
# Rendering the files together must produce exactly the concatenation of rendering
# each file on its own. This guards the file-boundary handling in main(): if the
# last block of one file were merged with the first block of the next, the two
# sides would differ. (This is about the multi-file *argument* path; piping a raw
# `cat f1 f2` is a single document by definition and is intentionally not covered.)
#
# Usage: tests/property-concat.sh [mdcat-binary] [file ...]
# Defaults: the ./mdcat next to the repo root, run over tests/*.md in sorted order.
# Exit status: 0 if the property holds, 1 otherwise (printing a diff).

set -eu

here=$(CDPATH= cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")

mdcat=${1:-$root/mdcat}
if [ "$#" -gt 0 ]; then shift; fi

if [ "$#" -gt 0 ]; then
    set -- "$@"
else
    # Default corpus: every sample in this directory, in sorted (shell glob) order,
    # which is the same order the all-at-once invocation will see.
    set -- "$here"/*.md
fi

if [ ! -x "$mdcat" ]; then
    echo "property-concat: $mdcat is not an executable; run 'make' first" >&2
    exit 2
fi

separate=$(mktemp)
together=$(mktemp)
trap 'rm -f "$separate" "$together"' EXIT

# Left side: render each file by itself and concatenate the outputs in order.
for f in "$@"; do
    "$mdcat" "$f" >> "$separate"
done

# Right side: render all files in one invocation.
"$mdcat" "$@" > "$together"

if diff -u "$separate" "$together" > /dev/null; then
    echo "property-concat: OK ($# file(s))"
    exit 0
fi

echo "property-concat: FAIL — combined output differs from concatenated per-file output" >&2
echo "  <  = per-file (mdcat f1)(mdcat f2)...   >  = combined (mdcat f1 f2 ...)" >&2
diff -u "$separate" "$together" >&2 || true
exit 1
