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
# With no W/H, timg uses its default fit.  Output goes straight to the terminal.
emit_sixel() {
    _png="$1"; _w="$2"; _h="$3"
    if [ -n "$_w" ] && [ -n "$_h" ]; then
        timg -ps -g"${_w}x${_h}" "$_png"
    else
        timg -ps "$_png"
    fi
}

# Read a terminal reply to a query.  Echoes the raw bytes (with ESC shown as the
# literal text "ESC") so they are safe to print.  Stops as soon as it sees the
# reply's terminating byte ($1, e.g. "t" for CSI..t reports or "R" for a cursor
# report) so a slow reply is never misattributed to the next query.  Falls back
# to an idle timeout if no terminator is given or none arrives.
# Usage: send a query to the terminal, then: reply=$(read_reply t)
read_reply() {
    _term="$1"
    _r=""
    # Read one byte at a time.  The first byte waits up to 2s for a slow reply;
    # subsequent bytes use a short timeout so we don't hang if it never ends.
    _to=2
    while IFS= read -r -s -t "$_to" -n 1 _c 2>/dev/null; do
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
