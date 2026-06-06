#!/bin/sh
# probe-cursor-after-sixel.sh — where does the text cursor land after a sixel?
#
# Why: the broken "step-6 trick" assumed the cursor sits in a predictable spot
# after a sixel.  We must know the truth: after timg emits a sixel of P pixels
# tall, does the cursor end at the BOTTOM-left of the image (advanced by
# ceil(P/cellH) rows), at the TOP-left (no advance), or somewhere else?  Does
# the terminal SCROLL when the image reaches the bottom?  Does sixel scrolling
# mode (DECSDM, CSI ?80h/l) change it?
#
# Method: print a row of known text, record the cursor row via CSI 6 n, emit a
# sixel of known pixel height, then immediately print a marker "<HERE" exactly
# where the cursor now is and record the cursor row again.  The delta in rows,
# plus where the marker visually lands relative to the image in a screenshot,
# answers the question.  We repeat with sixel scrolling mode on and off.
#
# How to read it: compare the two reported cursor rows (before/after) — their
# difference is how many rows the sixel advanced the cursor.  In the screenshot,
# note whether "<HERE" sits at the image's bottom-left (advanced) or overlaps
# the image top (no advance), and whether earlier text scrolled off.

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
require_tty

# Ask for the cursor position: CSI 6 n -> reply "ESC[row;colR".  Echo "row col".
cursor_pos() {
    rp=$(term_query '6n' R)
    echo "$rp" | sed -n 's/^ESC\[\([0-9]*\);\([0-9]*\)R.*/\1 \2/p'
}

run_case() {  # $1 = label, $2 = "on"|"off" sixel scrolling
    banner "case: sixel scrolling $2 ($1)"
    [ "$2" = on ]  && printf '%s?80h' "$CSI"   # DECSDM set: sixel scrolling on
    [ "$2" = off ] && printf '%s?80l' "$CSI"   # DECSDM reset

    printf 'TEXT-BEFORE (anchor row)\n'
    before=$(cursor_pos)
    printf 'cursor BEFORE sixel: row;col = [%s]\n' "$before"

    emit_sixel "$PROBE_PNG" 8 8     # ~8x8 cells, a compact known block

    # Mark wherever the cursor is now, then read it.
    printf '<HERE col0 is where cursor landed'
    after=$(cursor_pos)
    printf '\ncursor AFTER sixel:  row;col = [%s]\n' "$after"
    printf '(delta rows = AFTER.row - BEFORE.row tells the advance)\n'
}

run_case "default first" off
run_case "scrolling on"  on
# Restore a sane default.
printf '%s?80h' "$CSI"
printf '\nDone.  Compare the two cursor rows and screenshot the markers.\n'
