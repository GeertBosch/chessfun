#!/bin/sh
# probe-grid-image.sh — the real target shape: an image inside a text grid.
#
# Why: this mimics what mdcat must do for a table row — print a multi-column
# text grid, paint an image into ONE column's reserved block, and continue text
# below the whole row without the image bleeding up into the header or down into
# following text.
#
# Method: print a header row and a separator, reserve N rows for the body, write
# short text in columns 1 and 3, paint the image into column 2's cell using the
# strategy chosen from probe-absolute-vs-saverestore.sh (default: absolute), then
# resume below the reserved block and print a footer + separator.
#
# Strategy is selectable: PROBE_STRATEGY=abs|decsc|sco  (default abs).
#
# How to read it (screenshot): the image must sit entirely between the header
# separator and the footer separator, in the middle column, with the column-1
# and column-3 text intact and the footer on a clean row below everything.

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
require_tty
: "${PROBE_STRATEGY:=abs}"

cursor_pos() {
    rp=$(term_query '6n' R)
    echo "$rp" | sed -n 's/^ESC\[\([0-9]*\);\([0-9]*\)R.*/\1 \2/p'
}

COL2=20          # column (1-based) where the image cell starts
IMG_W=8          # image width in cells
IMG_H=8          # image height in cells -> body block height

banner "grid with one image cell (strategy=$PROBE_STRATEGY)"
printf 'COL-1 header     COL-2 (image)      COL-3 header\n'
printf -- '--------------  -----------------  --------------\n'

# Record the body's first row, then reserve IMG_H rows.
start=$(cursor_pos); startrow=$(echo "$start" | cut -d' ' -f1)
i=0; while [ "$i" -lt "$IMG_H" ]; do printf '\n'; i=$((i + 1)); done
printf '%s%dA' "$CSI" "$IMG_H"

# Text in column 1 (top of block) and column 3 (top of block).
printf 'left top'
printf '%s%dG' "$CSI" "$((COL2 + IMG_W + 2))"
printf 'right top'
printf '\r'

# Position to the image cell origin and paint, per strategy.
case "$PROBE_STRATEGY" in
  abs)
    [ -n "$startrow" ] && printf '%s%d;%dH' "$CSI" "$startrow" "$COL2"
    emit_sixel "$PROBE_PNG" "$IMG_W" "$IMG_H"
    [ -n "$startrow" ] && printf '%s%d;1H' "$CSI" "$((startrow + IMG_H))"
    ;;
  decsc)
    printf '%s%dG' "$CSI" "$COL2"
    printf '%s7' "$ESC"; emit_sixel "$PROBE_PNG" "$IMG_W" "$IMG_H"; printf '%s8' "$ESC"
    printf '%s%dB' "$CSI" "$IMG_H"
    ;;
  sco)
    printf '%s%dG' "$CSI" "$COL2"
    printf '%ss' "$CSI"; emit_sixel "$PROBE_PNG" "$IMG_W" "$IMG_H"; printf '%su' "$CSI"
    printf '%s%dB' "$CSI" "$IMG_H"
    ;;
esac

printf 'COL-1 footer     COL-2 footer       COL-3 footer\n'
printf -- '--------------  -----------------  --------------\n'
printf '\nDone.  Image should be boxed between the two separators, no bleed.\n'
