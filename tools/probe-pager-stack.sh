#!/bin/sh
# probe-pager-stack.sh — can an image be painted as a vertical stack of separate
# sixels, one per text row, and reassemble PIXEL-PERFECT (no missing/repeated
# pixel rows)?  This is the core feasibility question for line-at-a-time paging.
#
# The obstacle (see [[mdcat-sixel-terminal-behavior]]): a text cell is 14 px tall
# but sixel paints in 6 px bands.  A pager reveals content one TEXT ROW at a time
# and positions each row on a cell boundary (a multiple of 14 px).  To fill a
# 14 px row with sixel we must emit a whole number of 6 px bands: 12 px (2 bands,
# 2 px SHORT -> a gap) or 18 px (3 bands, 4 px OVER -> spills into the next row).
# The plan is to paint 18 px per row, TOP-DOWN, so each row's 4 px of overspill is
# overwritten by the next row's strip — which paints identical source pixels there.
# If the terminal paints each sixel starting exactly at the cursor cell's top
# pixel, the result is seamless.  This probe checks whether that actually holds.
#
# METHOD: build a tall diagnostic PNG with diagonal lines (a 1 px vertical
# misregistration at any seam shows as a visible STEP/kink in the diagonals) plus
# a vertical gradient (a skipped row shows as a brightness jump, a repeated row as
# a flat spot).  Then render it three ways, top to bottom on screen:
#
#   1. REFERENCE — the whole PNG as a single sixel.  Internally a sixel stacks its
#      own 6 px bands perfectly, so this is the gold standard to compare against.
#   2. STACK-18 — one 18 px sixel strip per cell row, painted top-down with the
#      overspill-overwrite scheme.  THIS is the pager model.  Should look
#      identical to the reference: continuous diagonals, smooth gradient.
#   3. STACK-12 — same but 12 px strips (a deliberately BROKEN control).  Each row
#      is 2 px short, so 2 px gaps should appear as white seams every 14 px.  This
#      proves the diagnostic image is actually sensitive to seams.
#
# HOW TO READ IT (screenshot): compare blocks 1 and 2 — if STACK-18 matches the
# REFERENCE with unbroken diagonals and no horizontal seams, line-at-a-time paging
# is feasible on this terminal.  Block 3 (STACK-12) should clearly show the gap
# seams the scheme is designed to avoid.
#
# Override the assumed cell height with PROBE_CELL_H (px); default 14 (VSCode).

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
command -v magick >/dev/null 2>&1 || { echo "ImageMagick 'magick' not found" >&2; exit 1; }
[ -t 1 ] || { echo "stdout is not a TTY; run this directly in a terminal" >&2; exit 1; }

: "${PROBE_CELL_H:=14}"
CELL="$PROBE_CELL_H"
ROWS=10                       # how many text rows tall the test image is
W=90                          # image width in px (multiple of 6 for clean bands)
H=$((CELL * ROWS))            # image height in px == exactly ROWS cells
TMP=$(mktemp -d)
SRC="$TMP/src.png"
trap 'rm -rf "$TMP"' EXIT INT TERM

# Build the diagnostic image: white bg, a black diagonal every 18 px (so a kink is
# obvious at any seam), overlaid on a faint black->white vertical gradient.
draw=""
k=-"$H"
while [ "$k" -lt "$H" ]; do
    draw="$draw -draw \"line 0,$k $W,$((k + H))\""
    k=$((k + 18))
done
eval magick -size "${W}x${H}" gradient:gray30-gray85 \
    -stroke black -strokewidth 1 $draw "$SRC"

# Emit the PNG region [y, y+h) as a native-pixel sixel (no scaling).
strip_sixel() {  # $1=y  $2=h
    magick "$SRC" -crop "${W}x${2}+0+${1}" +repage sixel:- 2>/dev/null
}

# Reserve ROWS rows of vertical space (scroll-safe) and park the cursor back at
# the top-left of that reserved band, ready to paint into it.
reserve_band() {
    i=0; while [ "$i" -lt "$ROWS" ]; do printf '\n'; i=$((i + 1)); done
    printf '%s%dA' "$CSI" "$ROWS"
}

# Paint the image as a stack of `$1`-px strips, one per cell row, top-down.  Each
# strip is emitted at the top of cell r (DECSC), then the cursor is restored
# (DECRC) and stepped down exactly one cell, so strip r+1 overwrites strip r's
# overspill with identical pixels.
paint_stack() {  # $1 = strip height in px
    sh="$1"; r=0
    while [ "$r" -lt "$ROWS" ]; do
        y=$((r * CELL))
        h="$sh"
        [ $((y + h)) -gt "$H" ] && h=$((H - y))   # clamp the last strips
        printf '\0337'                            # DECSC: save cursor
        strip_sixel "$y" "$h"
        printf '\0338'                            # DECRC: back to strip top
        printf '%s1B' "$CSI"                      # down exactly one cell
        r=$((r + 1))
    done
    printf '\r'                                   # column 1, just below the band
}

banner "1. REFERENCE — whole image as ONE sixel (gold standard)"
reserve_band
printf '\0337'; strip_sixel 0 "$H"; printf '\0338'
printf '%s%dB\r' "$CSI" "$ROWS"

banner "2. STACK-18 — pager model: 18px strips, top-down overwrite (should MATCH #1)"
reserve_band
paint_stack 18

banner "3. STACK-12 — BROKEN control: 12px strips (expect 2px gap seams every ${CELL}px)"
reserve_band
paint_stack 12

printf '\nDone. Screenshot all three. #2 should be indistinguishable from #1;\n'
printf '#3 should show the seams that the 18px overspill scheme avoids.\n'
printf '(assumed cell height: %s px — override with PROBE_CELL_H)\n' "$CELL"
