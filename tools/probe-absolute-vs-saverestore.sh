#!/bin/sh
# probe-absolute-vs-saverestore.sh — which cursor strategy survives a sixel?
#
# Why: to place an image inside a table cell and then keep drawing the rest of
# the grid, mdcat must return the cursor to a known spot after the sixel.  Two
# candidate strategies, both tested here on the same screen:
#
#   (a) DECSC / DECRC   : ESC 7  ... sixel ...  ESC 8   (save/restore cursor)
#   (b) CSI s / CSI u   : ESC[s  ... sixel ...  ESC[u   (SCO save/restore)
#   (c) absolute address: record start row R, emit sixel, ESC[R;1H to return
#
# Method: each block prints a labeled left margin of rows, paints the image a
# few columns in, then tries to resume text at a KNOWN position using the
# strategy.  If the strategy works, the "RESUMED:" text lands exactly where the
# label predicts (same row as the block's anchor, just below the image).  If it
# fails, the text lands on top of the image, or far away.
#
# How to read it (screenshot): for each of (a)(b)(c), does "RESUMED-x" appear on
# the expected row directly under that block's image, with no overlap?  The
# strategy whose RESUMED text is correctly placed is the one mdcat should use.

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
require_tty

cursor_pos() {
    printf '%s6n' "$CSI"
    rp=$(read_reply)
    echo "$rp" | sed -n 's/^ESC\[\([0-9]*\);\([0-9]*\)R.*/\1 \2/p'
}

# Reserve `rows` blank lines for an image block so resuming text never has to
# scroll; returns nothing.  We print rows then move back up to the top.
reserve_rows() {  # $1 = rows
    n=$1
    i=0; while [ "$i" -lt "$n" ]; do printf '\n'; i=$((i + 1)); done
    printf '%s%dA' "$CSI" "$n"
}

IMG_ROWS=8

banner "(a) DECSC / DECRC  (ESC 7 / ESC 8)"
printf 'anchor-a:\n'
reserve_rows "$IMG_ROWS"
printf '%s7' "$ESC"                 # DECSC save
emit_sixel "$PROBE_PNG" 8 "$IMG_ROWS"
printf '%s8' "$ESC"                 # DECRC restore
# Now move down past the reserved block and print resumed text.
printf '%s%dB' "$CSI" "$IMG_ROWS"
printf 'RESUMED-a (should be just below image a)\n'

banner "(b) CSI s / CSI u"
printf 'anchor-b:\n'
reserve_rows "$IMG_ROWS"
printf '%ss' "$CSI"                 # SCO save
emit_sixel "$PROBE_PNG" 8 "$IMG_ROWS"
printf '%su' "$CSI"                 # SCO restore
printf '%s%dB' "$CSI" "$IMG_ROWS"
printf 'RESUMED-b (should be just below image b)\n'

banner "(c) absolute addressing (record row, ESC[R;1H back)"
printf 'anchor-c:\n'
start=$(cursor_pos); startrow=$(echo "$start" | cut -d' ' -f1)
reserve_rows "$IMG_ROWS"
emit_sixel "$PROBE_PNG" 8 "$IMG_ROWS"
if [ -n "$startrow" ]; then
    printf '%s%d;1H' "$CSI" "$((startrow + IMG_ROWS))"
fi
printf 'RESUMED-c (should be just below image c)\n'

printf '\nDone.  Screenshot all three blocks; the correctly-placed RESUMED wins.\n'
