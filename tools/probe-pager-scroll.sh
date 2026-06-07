#!/bin/sh
# probe-pager-scroll.sh — a bidirectional mini-pager: the real scroll loop both
# ways. It scrolls DOWN through a synthetic document (an image enters from the
# bottom one 18px strip per scroll, rides up, and exits the top), then REVERSES
# and scrolls UP (the image re-enters from the TOP, a strip painted there per
# reverse-scroll while the rest scrolls down). This is essentially the pager core.
#
# It proves, in one run, everything "more" needs AND the one piece "less" adds:
#   - DOWN: scroll region up via LF at the bottom row, paint one fresh 18px strip
#     at the bottom; its 4px overspill drops on the pinned status row, wiped each
#     frame. (forward — already proven by the earlier probes, shown here in motion)
#   - UP: scroll region DOWN via Reverse Index (ESC M) at the top row, paint one
#     fresh strip at the TOP; its 4px overspill lands on the row below (the next
#     strip — identical pixels). Does the terminal scroll sixels DOWN cleanly too?
#     THIS is the new, unproven half — watch the UP phase closely.
#
# A DECSTBM scroll region [1, WIN] keeps a status bar pinned at row WIN+1
# throughout. Per [[mdcat-pager-stacking]].
#
# How to read it: the image must be seamless in BOTH directions — unbroken
# diagonals, smooth gradient, no seam where freshly painted strips meet scrolled
# ones, status bar always clean. If the UP phase is as clean as the DOWN phase,
# reverse scroll ("less") is locked.
#
# Env: PROBE_CELL_H(14), ROWS(image cells,8), DELAY(s,0.12), WIN(content rows,24).

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
command -v magick >/dev/null 2>&1 || { echo "ImageMagick 'magick' not found" >&2; exit 1; }
[ -t 1 ] || { echo "stdout is not a TTY; run this directly in a terminal" >&2; exit 1; }

: "${PROBE_CELL_H:=14}"; : "${ROWS:=8}"; : "${DELAY:=0.12}"; : "${WIN:=24}"
CELL="$PROBE_CELL_H"; W=120; H_IMG=$((CELL * ROWS))
TMP=$(mktemp -d); SRC="$TMP/src.png"
trap 'printf "%sr%s0m" "$CSI" "$CSI"; rm -rf "$TMP"' EXIT INT TERM

draw=""; k=-"$H_IMG"
while [ "$k" -lt "$H_IMG" ]; do draw="$draw -draw \"line 0,$k $W,$((k + H_IMG))\""; k=$((k + 18)); done
eval magick -size "${W}x${H_IMG}" gradient:gray25-gray85 -stroke black -strokewidth 1 $draw "$SRC"

strip() { magick "$SRC" -crop "${W}x${2}+0+${1}" +repage sixel:- 2>/dev/null; }

# Screen height H via a cursor-position report (park at the far corner, then CSI 6n).
printf '%s999;999H' "$CSI"
H=$(term_query '6n' R | sed -n 's/^ESC\[\([0-9]*\);[0-9]*R.*/\1/p')
: "${H:=40}"
[ "$WIN" -gt $((H - 2)) ] && WIN=$((H - 2))

IMG0="$WIN"; IMG1=$((WIN + ROWS)); STATUS=$((WIN + 1))   # image is the WIN..WIN+ROWS doc rows
REV=$(printf '%s7m' "$CSI"); NORM=$(printf '%s0m' "$CSI")

bar()         { printf '%s%d;1H%sK%s %s %s' "$CSI" "$STATUS" "$CSI" "$REV" "$1" "$NORM"; }
scroll_up()   { printf '%s%d;1H\n' "$CSI" "$WIN"; }       # LF at region bottom -> scroll up
scroll_down() { printf '%s1;1H%sM' "$CSI" "$ESC"; }       # RI at region top    -> scroll down

# Paint document row $2 at screen row $1 (image rows -> an 18px strip, else text).
paint_row() {
    printf '%s%d;1H%sK' "$CSI" "$1" "$CSI"
    if [ "$2" -ge "$IMG0" ] && [ "$2" -lt "$IMG1" ]; then
        s=$(( $2 - IMG0 )); y=$((s * CELL)); h=18; [ $((y + h)) -gt "$H_IMG" ] && h=$((H_IMG - y))
        printf '\0337'; strip "$y" "$h"; printf '\0338'
    else
        printf 'document line %d' $(( $2 + 1 ))
    fi
}

printf '%s2J' "$CSI"                         # clear
printf '%s1;%dr' "$CSI" "$WIN"               # DECSTBM: content region rows 1..WIN, status pinned

top=0                                        # doc index shown at screen row 1
r=1; while [ "$r" -le "$WIN" ]; do paint_row "$r" $((top + r - 1)); r=$((r + 1)); done
bar "ready"; sleep "$DELAY"

# DOWN: scroll until the image is fully past the top.
target=$((IMG1 + 3))
while [ "$top" -lt "$target" ]; do
    top=$((top + 1)); scroll_up; paint_row "$WIN" $((top + WIN - 1)); bar "DOWN  top=$top"; sleep "$DELAY"
done
# UP (the new half): scroll back to the start.
while [ "$top" -gt 0 ]; do
    top=$((top - 1)); scroll_down; paint_row 1 "$top"; bar "UP    top=$top"; sleep "$DELAY"
done

printf '%sr%s%d;1H%s0m\n' "$CSI" "$CSI" "$H" "$CSI"   # reset region, cursor to bottom
printf 'Done. Was the image seamless scrolling DOWN and UP, status bar clean?\n'
