#!/bin/sh
# gmore's cell-grid terminal emulator, checked deterministically via `--dump`
# (which renders the emulated grid to stdout with no paging, so no tty is needed).
# Each case feeds a byte sequence and asserts the reconstructed output, covering:
# plain/UTF-8 round-trip, CR and cursor-up overwrites, line wrap, SGR colour
# round-trip, and that OSC 8 + sixel DCS sequences are skipped without mangling
# surrounding text. Also checks the non-tty cat-passthrough path.
#
# Usage: tests/gmore-emulator.sh [gmore-binary]
# Exit status: 0 if all cases pass, 1 otherwise (printing diffs).

set -u

here=$(CDPATH= cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
gmore=${1:-$root/gmore}

if [ ! -x "$gmore" ]; then
    echo "gmore-emulator: $gmore is not an executable; run 'make' first" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fails=0
n=0

# check LABEL  EXPECTED_BYTES  -- runs gmore with the remaining args, stdin already
# redirected by the caller, and diffs stdout against EXPECTED_BYTES.
# (EXPECTED_BYTES and the input are produced with printf by the caller.)
check() {
    label=$1; expected=$2; shift 2
    printf '%b' "$expected" > "$tmp/exp"
    "$@" > "$tmp/act" 2> "$tmp/err"
    if diff -u "$tmp/exp" "$tmp/act" > /dev/null; then
        n=$((n + 1))
    else
        echo "gmore-emulator: FAIL [$label]" >&2
        echo "  expected | actual:" >&2
        diff -u "$tmp/exp" "$tmp/act" >&2 || true
        [ -s "$tmp/err" ] && { echo "  stderr:" >&2; sed 's/^/    /' "$tmp/err" >&2; }
        fails=$((fails + 1))
    fi
}

# dump COLS FILE — render FILE's emulated grid at the given width (width passed
# explicitly, not via an env prefix, to avoid var-assignment leaking across cases).
dump() { COLUMNS="$1" LINES=24 GMORE_CELLW=8 GMORE_CELLH=16 "$gmore" --dump "$2"; }
mkin() { printf '%b' "$1" > "$tmp/in"; }

# 1. plain text round-trips exactly
mkin 'hello\nworld\n'
check "plain text"       'hello\nworld\n'        dump 80 "$tmp/in"

# 2. carriage return overwrites from column 0: "abc\rX" -> "Xbc"
mkin 'abc\rX'
check "CR overwrite"     'Xbc\n'                 dump 80 "$tmp/in"

# 3. cursor-up then write overwrites the row above: line1 / line2 / up / "yy"
mkin 'line1\nline2\033[1Ayy'
check "cursor-up overwrite" 'line1yy\nline2\n'   dump 80 "$tmp/in"

# 4. lines longer than the width wrap onto the next row
mkin 'abcdefg\n'
check "wrap at width"    'abcd\nefg\n'           dump 4 "$tmp/in"

# 5. UTF-8 (é, 世) round-trips byte-for-byte
mkin '\303\251 \344\270\226\n'
check "utf-8 round-trip" '\303\251 \344\270\226\n' dump 80 "$tmp/in"

# 6. SGR colour is preserved and re-emitted minimally (red -> palette index 1)
mkin '\033[31mRED\033[0m x\n'
check "sgr colour"       '\033[0;38;5;1mRED\033[0m x\n' dump 80 "$tmp/in"

# 7. OSC 8 hyperlink is skipped, the link text is kept
mkin '\033]8;;https://example.com\033\\click\033]8;;\033\\ here\n'
check "osc8 skipped"     'click here\n'          dump 80 "$tmp/in"

# 8. a sixel DCS reserves vertical space (decoded + anchored, cursor advances below
#    the image); surrounding text is intact, just on its own rows. 6px tall @ cellH=16
#    -> 1 cell, so "after" lands on the next row.
mkin 'before\033Pq#0;2;0;0;0#0~~~\033\\after\n'
check "sixel reserves row" 'before\nafter\n'     dump 80 "$tmp/in"

# 9. non-tty, non-dump: pass the input through verbatim (pager-as-cat)
mkin 'raw\nbytes\n'
check "cat passthrough"  'raw\nbytes\n'          "$gmore" "$tmp/in"

if [ "$fails" -eq 0 ]; then
    echo "gmore-emulator: OK ($n cases)"
    exit 0
fi
echo "gmore-emulator: FAIL ($fails of $((n + fails)) cases)" >&2
exit 1
