#!/bin/sh
# probe-cell-size.sh — determine the terminal's character-cell pixel size.
#
# Why: mdcat converts pixels <-> character cells when sizing images.  The cell
# height in pixels also tells us how sixel bands (6 px each) align to text rows.
#
# Method A (preferred): ask the terminal directly.
#   CSI 16 t  -> reply "CSI 6 ; height ; width t"  (cell size in pixels)
#   CSI 14 t  -> reply "CSI 4 ; height ; width t"  (text area size in pixels)
#   CSI 18 t  -> reply "CSI 8 ; rows ; cols t"     (text area size in cells)
# Dividing area-pixels by area-cells gives cell size even if 16t is unsupported.
#
# Method B (fallback): render a sixel of a known pixel height with NO scaling
# and have the user count how many text rows it covers.  rows ~= pix / cellH.
#
# How to read it: the script prints the raw replies (ESC shown literally) and,
# if it could parse them, the derived cell width x height.  If replies are empty
# the terminal didn't answer; use Method B's printed instructions + a screenshot.

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$dir/probe-common.sh"
require_tty

query() {  # $1 = the CSI argument, e.g. "16t" -> sends ESC[16t and reads to 't'
    term_query "$1" t
}

banner "Method A: direct queries (raw replies, ESC shown literally)"

printf 'CSI 16 t (cell size px)  -> '; r16=$(query '16t'); printf '%s\n' "$r16"
printf 'CSI 14 t (text area px)  -> '; r14=$(query '14t'); printf '%s\n' "$r14"
printf 'CSI 18 t (text area cell)-> '; r18=$(query '18t'); printf '%s\n' "$r18"

# Try to parse "ESC6;H;Wt" from r16.
parse_2() {  # $1 reply, $2 expected leading code -> echoes "H W" or nothing
    echo "$1" | sed -n "s/^ESC\[*$2;\([0-9]*\);\([0-9]*\)t.*/\1 \2/p"
}

cell=$(parse_2 "$r16" 6)
if [ -n "$cell" ]; then
    set -- $cell
    printf '\n-> Cell size from CSI16t: %s px wide x %s px tall\n' "$2" "$1"
else
    area=$(parse_2 "$r14" 4)
    cells=$(parse_2 "$r18" 8)
    if [ -n "$area" ] && [ -n "$cells" ]; then
        set -- $area;  ah=$1; aw=$2
        set -- $cells; cr=$1; cc=$2
        [ "$cr" -gt 0 ] 2>/dev/null && [ "$cc" -gt 0 ] 2>/dev/null && \
          printf '\n-> Cell size derived (area/cells): %s px wide x %s px tall\n' \
                 "$((aw / cc))" "$((ah / cr))"
    else
        printf '\n-> No parseable reply.  Use Method B below.\n'
    fi
fi

banner "Method B: visual ruler (count rows the image covers)"
cat <<EOF
Below is a $PROBE_PNG rendered at native size (no -g scaling), preceded by a
numbered text ruler.  Count how many ruler rows the image spans top to bottom;
cellHeight ~= imageHeightPx / rowsCovered.  Take a screenshot if asked.
EOF
# Numbered ruler: 20 lines "01".."20" so the screenshot has a vertical scale.
i=1
while [ "$i" -le 20 ]; do printf '%02d\n' "$i"; i=$((i + 1)); done
# Move cursor back to the top of the ruler and paint the image starting at row 1.
printf '%s20A' "$CSI"
emit_sixel "$PROBE_PNG" "" ""
printf '\n'
