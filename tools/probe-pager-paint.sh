#!/bin/sh
# probe-pager-paint.sh — watch an image being painted ONE TEXT ROW AT A TIME, the
# way the pager will paint it, but slowed down so the incremental build is visible.
#
# Why: probe-pager-stack proved the 18px-strip stack reassembles perfectly, but it
# painted every strip instantly. probe-scroll-sixel proved a whole pre-rendered
# image scrolls cleanly. Neither showed the image being CONSTRUCTED strip by strip.
# This does: it paints the image top-down, one 18px strip per cell row, pausing
# DELAY seconds between strips, so you actually see it grow a line at a time and
# can confirm it still reconstructs pixel-perfect when drawn incrementally.
#
# What you'll see (and why it's correct): each freshly painted strip is 18px tall,
# so it briefly shows a ~4px "fringe" poking into the still-blank next row. The
# next strip (DELAY later) overwrites that fringe with identical source pixels —
# you watch the overspill scheme self-correct in real time. Only the very last
# strip leaves its 4px fringe (no successor); the pager handles that by reserving a
# status row below the image.
#
# How to read it: at the end the assembled image must be seamless — unbroken
# diagonals, smooth gradient, no horizontal seams between the strips. If so, the
# per-row paint the pager relies on is confirmed visually and end-to-end.
#
# Env: PROBE_CELL_H (px, default 14), ROWS (image height in cells, default 12),
#      DELAY (seconds between strips, default 0.12).

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
command -v magick >/dev/null 2>&1 || { echo "ImageMagick 'magick' not found" >&2; exit 1; }
[ -t 1 ] || { echo "stdout is not a TTY; run this directly in a terminal" >&2; exit 1; }

: "${PROBE_CELL_H:=14}"
: "${ROWS:=12}"
: "${DELAY:=0.12}"
CELL="$PROBE_CELL_H"
W=120
H=$((CELL * ROWS))
TMP=$(mktemp -d)
SRC="$TMP/src.png"
trap 'rm -rf "$TMP"' EXIT INT TERM

# Diagnostic image: diagonals every 18px over a vertical gradient.
draw=""; k=-"$H"
while [ "$k" -lt "$H" ]; do
    draw="$draw -draw \"line 0,$k $W,$((k + H))\""
    k=$((k + 18))
done
eval magick -size "${W}x${H}" gradient:gray25-gray85 \
    -stroke black -strokewidth 1 $draw "$SRC"

strip_sixel() {  # $1=y  $2=h  -> native-pixel sixel of the [y, y+h) slice
    magick "$SRC" -crop "${W}x${2}+0+${1}" +repage sixel:- 2>/dev/null
}

printf 'Painting the image one 18px strip per row, %ss apart. Watch it build top-down.\n' "$DELAY"

# Reserve the band (scroll-safe) and park at its top-left.
i=0; while [ "$i" -lt "$ROWS" ]; do printf '\n'; i=$((i + 1)); done
printf '%s%dA' "$CSI" "$ROWS"

# Paint each row's 18px strip, top-down, pausing between them. DECSC/DECRC keeps the
# cursor at each strip's top; CSI 1B steps down exactly one cell for the next strip.
r=0
while [ "$r" -lt "$ROWS" ]; do
    y=$((r * CELL)); h=18
    [ $((y + h)) -gt "$H" ] && h=$((H - y))    # clamp the last strip(s)
    printf '\0337'; strip_sixel "$y" "$h"; printf '\0338'
    printf '%s1B' "$CSI"
    sleep "$DELAY"
    r=$((r + 1))
done

printf '\r%sK' "$CSI"                          # clear the last strip's 4px fringe
printf 'Done. Is the assembled image seamless (unbroken diagonals, smooth gradient)?\n'
