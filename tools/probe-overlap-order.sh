#!/bin/sh
# probe-overlap-order.sh — does painting a strip OVER an existing neighbor clobber
# it? This is the crux of whether incremental UP scrolling can work.
#
# The 18px-overspill scheme assumes painting strip A (which overspills 4px into the
# cell below) only OVERWRITES those 4px — and since the neighbor strip B already
# has identical pixels there, it stays seamless. That holds when you paint
# top-down (probe-pager-paint), because the lower cell is painted AFTER. But
# incremental UP scrolling paints the NEW top strip over a neighbor that is ALREADY
# there (scrolled in earlier). If a sixel write clears the WHOLE cell it touches
# (not just the 4px), that erases the neighbor's top -> the gaps we saw.
#
# Test: paint the SAME two adjacent strips in the two possible orders, side by side.
#   LEFT  (lower-first): strip1 at row R+1, THEN strip0 at row R  (the UP order:
#         strip0's overspill lands on already-present strip1)
#   RIGHT (upper-first): strip0 at row R,   THEN strip1 at row R+1 (the top-down
#         order, known seamless from probe-pager-paint)
# Both should depict source rows [0,28) across rows R..R+1.
#
# How to read it: if LEFT looks identical to RIGHT (both seamless, diagonals
# continuous across the R/R+1 boundary), then overspill only overwrites the 4px
# and incremental UP is viable — the earlier gaps are a different bug. If LEFT
# shows a gap/clipped strip at the boundary while RIGHT is clean, a sixel write
# clobbers the whole touched cell, so painting over a live neighbor can't work and
# UP must repaint.
#
# Env: PROBE_CELL_H(14).

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
command -v magick >/dev/null 2>&1 || { echo "ImageMagick 'magick' not found" >&2; exit 1; }
[ -t 1 ] || { echo "stdout is not a TTY; run this directly in a terminal" >&2; exit 1; }

: "${PROBE_CELL_H:=14}"
CELL="$PROBE_CELL_H"; W=84; H_IMG=$((CELL * 4))     # source tall enough for source[0,32)
TMP=$(mktemp -d); SRC="$TMP/src.png"
trap 'rm -rf "$TMP"' EXIT INT TERM

draw=""; k=-"$H_IMG"
while [ "$k" -lt "$H_IMG" ]; do draw="$draw -draw \"line 0,$k $W,$((k + H_IMG))\""; k=$((k + 18)); done
eval magick -size "${W}x${H_IMG}" gradient:gray25-gray85 -stroke black -strokewidth 1 $draw "$SRC"

strip() { magick "$SRC" -crop "${W}x18+0+${1}" +repage sixel:- 2>/dev/null; }
paint() { printf '%s%d;%dH\0337' "$CSI" "$1" "$2"; strip "$3"; printf '\0338'; }  # row,col,srcY

COLR=18                                   # right block starts at column 18 (84px/6=14 cols wide)
printf '%s2J%sH' "$CSI" "$CSI"
printf 'LEFT = lower-first (the UP order)        RIGHT = upper-first (top-down)\n'

R=4                                       # top row of the two-strip pair
# strip0 = source[0,18), strip1 = source[14,32)  -> together they show source[0,32)
# LEFT: paint the LOWER strip first, then the UPPER overspilling onto it.
paint $((R + 1)) 1     14
paint   "$R"     1      0
# RIGHT: paint the UPPER first, then the LOWER (the known-good top-down order).
paint   "$R"     "$COLR"      0
paint $((R + 1)) "$COLR"     14

printf '%s%d;1H' "$CSI" $((R + 4))
printf 'Do LEFT and RIGHT look identical (both seamless across the row boundary)?\n'
printf 'If LEFT has a gap/clip but RIGHT is clean, painting over a live neighbor clobbers it.\n'
