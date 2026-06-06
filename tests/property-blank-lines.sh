#!/bin/sh
# Property: the number of blank lines in the output is invariant over the render width.
#
#   count("^$" in  mdcat --width N <files>)   is the same for every N
#
# Blank lines come from block structure — the separators around paragraphs, headings,
# code blocks and tables — which does not depend on the width. Wrapping only changes
# how many *non-blank* lines a block occupies; it must never add or drop a blank line.
# A change to reflow that emitted a stray empty line (or swallowed a separator) at some
# widths would break this, so we compare the blank-line count across a spread of widths.
#
# Usage: tests/property-blank-lines.sh [mdcat-binary] [file ...]
# Defaults: the ./mdcat next to the repo root, run over tests/*.md in sorted order.
# Exit status: 0 if the count is identical at every width, 1 otherwise.

set -eu

here=$(CDPATH= cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")

mdcat=${1:-$root/mdcat}
if [ "$#" -gt 0 ]; then shift; fi

if [ "$#" -gt 0 ]; then
    set -- "$@"
else
    set -- "$here"/*.md
fi

if [ ! -x "$mdcat" ]; then
    echo "property-blank-lines: $mdcat is not an executable; run 'make' first" >&2
    exit 2
fi

# A few widths from very narrow (forces heavy wrapping) to wide (little or none). The
# point of this property is invariance *across* width, so more than one is compared.
widths="10 44 200"

baseline=""
status=0
for n in $widths; do
    count=$("$mdcat" --width "$n" "$@" | grep -c "^$" || true)
    if [ -z "$baseline" ]; then
        baseline=$count
        baseline_width=$n
    elif [ "$count" != "$baseline" ]; then
        echo "property-blank-lines: FAIL — width $n has $count blank lines," \
             "but width $baseline_width has $baseline" >&2
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "property-blank-lines: OK ($# file(s), $baseline blank lines at widths: $widths)"
fi
exit "$status"
