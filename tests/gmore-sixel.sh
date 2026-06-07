#!/bin/sh
# gmore's sixel decoder + image anchoring, checked deterministically via
# `--imginfo` (parses input, prints each decoded image's anchor row/col, pixel
# size, cell footprint, and — for small images — an ASCII raster: '#' opaque,
# '.' unset). Inputs are hand-written sixel DCS strings, so no external image
# tools are needed. Cell size is pinned via GMORE_CELLW/GMORE_CELLH so the cell
# footprint is deterministic.
#
# Usage: tests/gmore-sixel.sh [gmore-binary]
# Exit status: 0 if all cases pass, 1 otherwise (printing diffs).

set -u

here=$(CDPATH= cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
gmore=${1:-$root/gmore}

if [ ! -x "$gmore" ]; then
    echo "gmore-sixel: $gmore is not an executable; run 'make' first" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fails=0
n=0

check() {
    label=$1; expected=$2; shift 2
    printf '%b' "$expected" > "$tmp/exp"
    "$@" > "$tmp/act" 2> "$tmp/err"
    if diff -u "$tmp/exp" "$tmp/act" > /dev/null; then
        n=$((n + 1))
    else
        echo "gmore-sixel: FAIL [$label]" >&2
        diff -u "$tmp/exp" "$tmp/act" >&2 || true
        [ -s "$tmp/err" ] && { echo "  stderr:" >&2; sed 's/^/    /' "$tmp/err" >&2; }
        fails=$((fails + 1))
    fi
}

ii()  { GMORE_CELLW=8 GMORE_CELLH=16 "$gmore" --imginfo "$1"; }   # full --imginfo
ii1() { ii "$1" | head -1; }                                      # first (summary) line
iig() { ii "$1" | grep '^image'; }                                # just the summary lines
mkin() { printf '%b' "$1" > "$tmp/in"; }

# a 4x6 solid block (~ = all six bits) -> every pixel opaque
mkin '\033Pq"1;1;4;6#0;2;100;0;0#0~~~~\033\\'
check "solid 4x6" \
    'image 1 @0,0 4x6px 1x1cells\n####\n####\n####\n####\n####\n####\n' \
    ii "$tmp/in"

# @ = bit 0 only -> just the TOP pixel row of the band is set
mkin '\033Pq"1;1;4;6#0;2;100;0;0#0@@@@\033\\'
check "top bit only" \
    'image 1 @0,0 4x6px 1x1cells\n####\n....\n....\n....\n....\n....\n' \
    ii "$tmp/in"

# '-' advances a band: two bands -> 12px tall
mkin '\033Pq"1;1;4;12#0;2;100;0;0#0~~~~-~~~~\033\\'
check "two bands tall" 'image 1 @0,0 4x12px 1x1cells\n' ii1 "$tmp/in"

# run-length !4~ is equivalent to ~~~~
mkin '\033Pq"1;1;4;6#0;2;100;0;0#0!4~\033\\'
check "run-length" 'image 1 @0,0 4x6px 1x1cells\n' ii1 "$tmp/in"

# anchor column tracks the cursor: two leading chars -> column 2
mkin 'XY\033Pq"1;1;4;6#0;2;100;0;0#0~~~~\033\\'
check "anchor column" 'image 1 @0,2 4x6px 1x1cells\n' ii1 "$tmp/in"

# grid protocol: a sixel leaves the cursor below the image; the producer's trailing
# LF is absorbed, so after LF + up1 + right8 the next image shares the SAME row.
# (Without the LF-absorb, the second image would drift to row 1 — the timg grid bug.)
mkin '\033Pq"1;1;4;6#0;2;100;0;0#0~~~~\033\\\n\033[1A\033[8C\033Pq"1;1;4;6#0;2;0;100;0#0~~~~\033\\'
check "grid same row" \
    'image 1 @0,0 4x6px 1x1cells\nimage 2 @0,8 4x6px 1x1cells\n' \
    iig "$tmp/in"

if [ "$fails" -eq 0 ]; then
    echo "gmore-sixel: OK ($n cases)"
    exit 0
fi
echo "gmore-sixel: FAIL ($fails of $((n + fails)) cases)" >&2
exit 1
