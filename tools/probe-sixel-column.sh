#!/bin/sh
# probe-sixel-column.sh — does this terminal honor the cursor COLUMN for sixels?
#
# Why (blocking question for tables): to place images side by side in table
# columns, mdcat must move the cursor to a column and have the sixel paint
# there.  Some terminals honor the cursor column; others always paint sixels at
# the left margin (column 1), which would make column placement impossible.
# probe-grid-image.sh showed the image at the left margin — this isolates why.
#
# It also verifies cursor-position reads work inside command substitution (the
# grid probe relies on that), by printing the captured value.
#
# Three explicit tests, each on its own labeled row:
#   T1: move to column 20, print '@', then emit a sixel WITHOUT moving.
#       -> image at col 20 (under/right of @) = column honored;
#          image at col 1 (far left)          = column IGNORED.
#   T2: same, column 40, to confirm it's not a fixed offset.
#   T3: report a CPR read taken inside $(...) so we know reads work there.
#
# How to read it (screenshot): note where each image's LEFT edge sits relative
# to its '@' marker.  Aligned with '@' = honored.  At the far left = ignored.

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
require_tty

cursor_pos() {
    rp=$(term_query '6n' R)
    echo "$rp" | sed -n 's/^ESC\[\([0-9]*\);\([0-9]*\)R.*/\1 \2/p'
}

col_test() {  # $1 = column
    c="$1"
    banner "column $c test"
    # Reserve 4 rows so the image has room and the next test isn't overwritten.
    printf '\n\n\n\n'; printf '%s4A' "$CSI"
    printf '%s%dG' "$CSI" "$c"      # cursor to column c
    printf '@'                       # visible marker AT column c
    printf '%s%dG' "$CSI" "$c"      # back to column c (printing '@' advanced it)
    emit_sixel "$PROBE_PNG" 4 3      # small image, no cursor move before it
    printf '%s4B\r' "$CSI"           # drop below the reserved block
    printf '(image left edge: AT the @ = column honored; FAR LEFT = ignored)\n'
}

banner "CPR read inside command substitution"
pos=$(cursor_pos)
printf 'captured cursor pos (row col) = [%s]\n' "$pos"
printf '(if empty, reads fail in $(...) here; if numbers, reads work)\n'

col_test 20
col_test 40

printf '\nDone.  Compare each image left edge to its @ marker.\n'
