#!/bin/sh
# probe-overshoot.sh — does the last sixel band spill into the next text row?
#
# Why (future pager work): sixel paints in bands of 6 px.  A text cell is, say,
# 14 px tall — not a multiple of 6.  An image meant to fill N rows is 14*N px,
# needing ceil(14*N/6) bands; the final band can extend past the cell's bottom
# edge into the next text row.  Before building a line-at-a-time pager we must
# know whether this overshoot happens and whether reprinting the row below
# cleanly overwrites the stray pixels.
#
# Method: pick an image pixel height that is NOT a multiple of the cell height
# (we use cell height as reported by probe-cell-size; default 14).  Paint it,
# then move to the row immediately below and print a full-width line of '#'.
# First we print that line BEFORE a screenshot, then (second block) we reprint
# it to see if the overwrite removes any sixel pixels that leaked down.
#
# How to read it (screenshot): in block 1, look at the seam between the image's
# bottom and the '#' line — are there leftover image pixels above/among the '#'?
# In block 2 (reprinted line) are they gone?  This tells us if overscan cleanup
# by reprinting works.
#
# Override the assumed cell height with PROBE_CELL_H (px).

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
require_tty
: "${PROBE_CELL_H:=14}"

# Choose rows so total pixel height is deliberately not a band/cell multiple.
ROWS=3
HASHES='########################################'   # 40 wide

banner "overshoot test (assumed cell height ${PROBE_CELL_H}px, ${ROWS} rows)"
printf 'Image below spans ~%d rows (%d px). Watch the bottom seam.\n' \
       "$ROWS" "$((PROBE_CELL_H * ROWS))"

# Reserve rows, paint image at native aspect scaled to ROWS rows of width 8.
i=0; while [ "$i" -le "$ROWS" ]; do printf '\n'; i=$((i + 1)); done
printf '%s%dA' "$CSI" "$((ROWS + 1))"
emit_sixel "$PROBE_PNG" 8 "$ROWS"

# Move to the row right below the image and lay down a hash line.
printf '%s%dB' "$CSI" "$ROWS"
printf '\r%s  <- hash line directly below image (look for leaked pixels)\n' "$HASHES"

banner "reprint the seam line to test overscan cleanup"
printf '%s1A' "$CSI"                       # back up onto the hash line
printf '\r%s  <- REPRINTED (did leaked pixels disappear?)\n' "$HASHES"
printf '\nDone.  Screenshot both blocks and compare the bottom seam.\n'
