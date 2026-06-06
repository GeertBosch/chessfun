#!/bin/sh
# Property: the two ways of forcing the render width agree.
#
#   mdcat --width N <files>   ==   COLUMNS=N mdcat <files>
#
# The --width flag and the $COLUMNS environment variable both override the detected
# terminal size, and they must resolve to exactly the same width — so for any N the
# two invocations produce byte-identical output. This guards the width-precedence
# logic in terminalWidth(): if --width and $COLUMNS were read differently (different
# parsing, different clamping, one ignored under some stdio condition), the sides
# would diverge.
#
# Usage: tests/property-width.sh [mdcat-binary] [file ...]
# Defaults: the ./mdcat next to the repo root, run over tests/*.md in sorted order,
# at a handful of representative widths.
# Exit status: 0 if the property holds at every width, 1 otherwise (printing a diff).

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
    echo "property-width: $mdcat is not an executable; run 'make' first" >&2
    exit 2
fi

# One non-standard width is enough to catch a divergence; 44 forces plenty of wrapping.
widths="44"

flag_out=$(mktemp)
env_out=$(mktemp)
trap 'rm -f "$flag_out" "$env_out"' EXIT

status=0
for n in $widths; do
    "$mdcat" --width "$n" "$@" > "$flag_out"
    COLUMNS="$n" "$mdcat" "$@" > "$env_out"
    if ! diff -u "$flag_out" "$env_out" > /dev/null; then
        echo "property-width: FAIL at width $n — --width and COLUMNS differ" >&2
        echo "  <  = mdcat --width $n   >  = COLUMNS=$n mdcat" >&2
        diff -u "$flag_out" "$env_out" >&2 || true
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "property-width: OK ($# file(s) at widths: $widths)"
fi
exit "$status"
