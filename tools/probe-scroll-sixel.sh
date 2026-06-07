#!/bin/sh
# probe-scroll-sixel.sh — does the terminal SCROLL an already-painted sixel
# cleanly, by exactly one text cell, together with the text?
#
# Why this is THE question for an image pager: to scroll the view down one line,
# the normal terminal behavior is to scroll the whole screen up one cell (a
# newline at the bottom row) and let us repaint only the newly-exposed bottom
# line. That makes paging cheap — a SINGLE-line repaint per scroll, no full
# screen redraw — BUT only if the terminal moves the already-rendered sixel
# RASTER up by exactly cellH px with no tearing, duplication, or dropping. If it
# does, the 18px-overspill stacking scheme ([[mdcat-pager-stacking]]) needs only
# the new bottom strip each step. If it doesn't, we must full-repaint instead.
#
# Method: clear the screen, paint a tall diagnostic image at the top (diagonals
# over a gradient — any tear shows as a kink, any duplicated/dropped pixel row as
# a seam/jump), then push it UP the screen one line at a time by printing filler
# at the bottom row with a short delay, so the scroll is watchable. Watch the
# rising image.
#
# How to read it: if the image stays pixel-identical to its first frame at EVERY
# step as it rises, hardware scroll is clean -> single-line-repaint paging works.
# Any tearing/seam/ghosting/freeze/vanish at the scroll steps means the terminal
# does not scroll sixels cleanly and the pager must full-repaint.
#
# Env: PROBE_CELL_H (px, default 14), ITER (scroll steps, default 30),
#      DELAY (seconds between steps, default 0.12).

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
command -v magick >/dev/null 2>&1 || { echo "ImageMagick 'magick' not found" >&2; exit 1; }
[ -t 1 ] || { echo "stdout is not a TTY; run this directly in a terminal" >&2; exit 1; }

: "${PROBE_CELL_H:=14}"
: "${ITER:=30}"
: "${DELAY:=0.12}"
CELL="$PROBE_CELL_H"
ROWS=6                         # image height in cells
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

# Over-fill far past any real screen height so the screen MUST overflow and the
# cursor is genuinely at the bottom of a full screen — then every later newline
# forces a real one-cell scroll of the whole screen, image included. (Trusting
# `tput lines` failed: it reported a tiny height, so the output fit without ever
# scrolling and the image never had to move.) This initial fill prints fast with
# no delay; it just establishes a full screen.
: "${FILL:=250}"

printf '%s2J%sH' "$CSI" "$CSI"     # clear screen, home
i=1
while [ "$i" -le "$FILL" ]; do
    printf 'filler %d (scrolls off the top)\n' "$i"; i=$((i + 1))
done

# Paint the image into the bottom ROWS rows of the now-full screen. Reserving with
# newlines scrolls the screen up ROWS lines; we then go back up and paint.
i=0; while [ "$i" -lt "$ROWS" ]; do printf '\n'; i=$((i + 1)); done
printf '%s%dA' "$CSI" "$ROWS"
printf '\0337'; magick "$SRC" sixel:- 2>/dev/null; printf '\0338'
printf '%s%dB\r' "$CSI" "$ROWS"

# Stream lines at the bottom of the full screen: each forces a one-cell scroll, so
# the image should rise one line per step. Watch whether it stays pixel-identical.
n=1
while [ "$n" -le "$ITER" ]; do
    printf 'scroll step %d/%d  <- image should be %d lines higher, pixel-identical\n' \
           "$n" "$ITER" "$n"
    sleep "$DELAY"
    n=$((n + 1))
done

printf 'Done. Did the image rise cleanly every step? (tear/seam/ghost/freeze = dirty)\n'
