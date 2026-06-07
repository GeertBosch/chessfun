#!/bin/sh
# probe-pager-fullscreen.sh — bidirectional scroll WITHOUT a scroll region.
#
# probe-pager-scroll failed (even gaps, both directions). The only new ingredient
# vs the probes that worked was a DECSTBM scroll region (used to pin a status bar).
# Hypothesis: VSCode/xterm.js carries sixels on a FULL-SCREEN scroll (proven by
# probe-scroll-sixel) but NOT on a region/margin scroll — so the region moved text
# while leaving the sixels behind, producing the gaps. This retries the same
# bidirectional pager using full-screen scrolling only, no region.
#
# PART 1 (DOWN): full-screen scroll up (LF at the last row) and paint one 18px
# strip at row H-1 per step, clearing the bottom scratch row (which absorbs the
# 4px overspill). The image scrolls in from the bottom.
# PART 2 (UP): full-screen scroll down (Reverse Index, ESC M, at the top row) and
# paint one strip at row 1 per step. The image scrolls in from the top. (This also
# tests whether full-screen REVERSE scroll carries sixels — the last unknown.)
#
# How to read it: both phases must be seamless — unbroken diagonals, smooth
# gradient, no horizontal gaps between strips. If so, full-screen scroll + per-row
# strips works both ways and the pager design is locked (no region, transient
# status only). If only DOWN is clean, "more" is confirmed and we defer "less".
#
# Env: PROBE_CELL_H(14), ROWS(image cells,8), DELAY(s,0.12).

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
command -v magick >/dev/null 2>&1 || { echo "ImageMagick 'magick' not found" >&2; exit 1; }
[ -t 1 ] || { echo "stdout is not a TTY; run this directly in a terminal" >&2; exit 1; }

: "${PROBE_CELL_H:=14}"; : "${ROWS:=8}"; : "${DELAY:=0.12}"
CELL="$PROBE_CELL_H"; W=120; H_IMG=$((CELL * ROWS))
TMP=$(mktemp -d); SRC="$TMP/src.png"
trap 'printf "%s0m" "$CSI"; rm -rf "$TMP"' EXIT INT TERM

draw=""; k=-"$H_IMG"
while [ "$k" -lt "$H_IMG" ]; do draw="$draw -draw \"line 0,$k $W,$((k + H_IMG))\""; k=$((k + 18)); done
eval magick -size "${W}x${H_IMG}" gradient:gray25-gray85 -stroke black -strokewidth 1 $draw "$SRC"

strip() { magick "$SRC" -crop "${W}x${2}+0+${1}" +repage sixel:- 2>/dev/null; }
strip_hw() { s=$1; y=$((s * CELL)); h=18; [ $((y + h)) -gt "$H_IMG" ] && h=$((H_IMG - y)); STRIP_Y=$y; STRIP_H=$h; }

printf '%s999;999H' "$CSI"
H=$(term_query '6n' R | sed -n 's/^ESC\[\([0-9]*\);[0-9]*R.*/\1/p')
: "${H:=40}"

fill_screen() { printf '%s2J%sH' "$CSI" "$CSI"; i=1; while [ "$i" -le $((H + 5)) ]; do printf 'document line %d\n' "$i"; i=$((i + 1)); done; }
fscroll_up()   { printf '%s%d;1H\n' "$CSI" "$H"; }        # LF at last row -> full screen up
fscroll_down() { printf '%s1;1H%sM' "$CSI" "$ESC"; }      # RI at top row  -> full screen down

put_bottom() {  # paint strip $1 at row H-1, then clear the scratch row H (absorbs overspill)
    strip_hw "$1"
    printf '%s%d;1H%sK' "$CSI" $((H - 1)) "$CSI"
    printf '\0337'; strip "$STRIP_Y" "$STRIP_H"; printf '\0338'
    printf '%s%d;1H%sK' "$CSI" "$H" "$CSI"
}
put_top() {     # paint strip $1 at row 1 (overspill into row 2 = next strip, identical)
    strip_hw "$1"
    printf '%s1;1H%sK' "$CSI" "$CSI"
    printf '\0337'; strip "$STRIP_Y" "$STRIP_H"; printf '\0338'
}

# PART 1 — DOWN: image scrolls in from the bottom.
fill_screen
s=0; while [ "$s" -lt "$ROWS" ]; do fscroll_up; put_bottom "$s"; sleep "$DELAY"; s=$((s + 1)); done
n=1; while [ "$n" -le 6 ]; do fscroll_up; printf '%s%d;1H%sKdocument (after image)' "$CSI" $((H - 1)) "$CSI"; printf '%s%d;1H%sK' "$CSI" "$H" "$CSI"; sleep "$DELAY"; n=$((n + 1)); done
printf '%s%d;1HPART 1 (DOWN) done. Press Enter for PART 2 (UP)…' "$CSI" "$H"; read _ </dev/tty

# PART 2 — UP: image scrolls in from the top (paint strips bottom-first so strip 0 ends on top).
fill_screen
s=$((ROWS - 1)); while [ "$s" -ge 0 ]; do fscroll_down; put_top "$s"; sleep "$DELAY"; s=$((s - 1)); done
n=1; while [ "$n" -le 6 ]; do fscroll_down; printf '%s1;1H%sKdocument (before image)' "$CSI" "$CSI"; sleep "$DELAY"; n=$((n + 1)); done

printf '%s%d;1H%s0m\n' "$CSI" "$H" "$CSI"
printf 'Done. Were BOTH phases seamless (no gaps between strips)?\n'
