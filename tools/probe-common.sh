# Shared helpers for the sixel probe scripts.  Sourced, not executed.
#
# These scripts characterize how a terminal positions the cursor around sixel
# graphics so mdcat can place images precisely on the text grid.  They are kept
# as durable diagnostics: run them on a new terminal (VSCode, iTerm, xterm) and
# read the markers / screenshots to learn that terminal's behavior.
#
# Conventions:
#   ESC is the escape byte.  We build escapes with printf '\033'.
#   A "marker" is a visible glyph we print at a known place so a screenshot
#   shows where the cursor actually was.

ESC=$(printf '\033')
CSI="${ESC}["
DCS_ST="${ESC}\\"          # String Terminator (ESC \)

# A small known PNG to render.  Override with $PROBE_PNG.
: "${PROBE_PNG:=tests/chess-piece.png}"   # 64x64 grayscale knight

# Emit a sixel for $1 scaled to fit $2 x $3 character cells (timg -g WxH).
# With no W/H, timg uses its default fit.
#
# CRITICAL: this CAPTURES timg's output to a file and then replays the bytes,
# exactly as mdcat does (runTimg uses popen to a pipe).  When timg's stdout is
# NOT a tty it emits plain sixel and does NOT do its own interactive cursor /
# scrolling management; replaying the captured bytes then paints the image at
# wherever the cursor currently sits.  Running timg straight to the tty instead
# lets timg reposition the image itself (it ignores our cursor) — which is wrong
# for placing images in a grid.  Note: with no tty, timg uses its DEFAULT cell
# size for -g, so the painted pixel size is read from the sixel raster
# attributes ("...;Ph;Pv) rather than assumed (see sixel_pixels).
emit_sixel() {
    _png="$1"; _w="$2"; _h="$3"
    _tmp=$(mktemp 2>/dev/null || echo /tmp/probe-sixel.$$)
    if [ -n "$_w" ] && [ -n "$_h" ]; then
        timg -ps -g"${_w}x${_h}" "$_png" > "$_tmp" 2>/dev/null
    else
        timg -ps "$_png" > "$_tmp" 2>/dev/null
    fi
    cat "$_tmp"
    rm -f "$_tmp"
}

# Echo "Ph Pv" — the painted pixel width and height parsed from a captured sixel
# file's raster attributes ("Pan;Pad;Ph;Pv).  Empty if not found.
sixel_pixels() {  # $1 = file containing sixel
    LC_ALL=C sed -n 's/.*"[0-9]*;[0-9]*;\([0-9]*\);\([0-9]*\).*/\1 \2/p' "$1" | head -1
}

# Send a query to the terminal and read its reply.  CRITICAL: the query is
# written to /dev/tty and the reply read from /dev/tty, NOT stdout/stdin — so
# this works correctly inside command substitution `x=$(term_query ...)`, where
# stdout is captured and a query printed to stdout would never reach the
# terminal.  Returns the reply with ESC shown as the literal text "ESC".
#
# $1 = the bytes to send after CSI (e.g. "6n" -> sends ESC[6n)
# $2 = the reply's terminating byte (e.g. "R" or "t"); reading stops once seen,
#      so a slow reply is never misattributed to a later query.
term_query() {
    _q="$1"; _term="$2"; _r=""
    printf '%s%s' "$CSI" "$_q" > /dev/tty
    # First byte waits up to 2s for a slow reply; later bytes use a short
    # timeout so we never hang if the terminator never arrives.
    _to=2
    while IFS= read -r -s -t "$_to" -n 1 _c < /dev/tty 2>/dev/null; do
        _to=1
        case "$_c" in
            "$ESC") _r="${_r}ESC" ;;
            "") ;;
            *) _r="${_r}${_c}"
               [ -n "$_term" ] && [ "$_c" = "$_term" ] && break ;;
        esac
    done
    printf '%s' "$_r"
}

# Print a banner separating probe sections.
banner() { printf '\n===== %s =====\n' "$1"; }

# Require timg and a TTY; bail out clearly otherwise.
require_tty() {
    command -v timg >/dev/null 2>&1 || { echo "timg not found in PATH" >&2; exit 1; }
    [ -t 1 ] || { echo "stdout is not a TTY; run this directly in a terminal" >&2; exit 1; }
    [ -f "$PROBE_PNG" ] || { echo "PROBE_PNG not found: $PROBE_PNG" >&2; exit 1; }
}
