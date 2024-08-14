# Show initial examples using plain ASCII text
# The Apple Terminal was used in the README.md.

source "$(dirname $BASH_SOURCE)/../textfun.sh"

FEN2TXT="${FEN2TXT:-$(dirname $BASH_SOURCE)/../fen2txt}"
ID=m5
IMGBASE=img/$ID

img_url() { echo "<img src=\"$IMGBASE-$1.png\" height=\"$(( $2 * 15 ))\" alt=\"$ID - $3\">" ; }

FEN=4rb1k/2pqn2p/6pn/ppp3N1/P1QP2b1/1P2p3/2B3PP/B3RRK1
# puzzle from https://lichess.org/study/Vvcgj8pb

echo This script must be run once each from an Apple Terminal.app terminal, a VSCode terminal,
echo and an actual X11 xterm program to reproduce the output for the README.

if [ "$TERM_PROGRAM" = vscode ] ; then
    img_url 1 8 "VS Code $TERM_PROGRAM_VERSION"
    time "$FEN2TXT" -sbrown $FEN
elif [ "${XTERM_VERSION/(*)/}" = XTerm ] ; then
    img_url 2 8 "${XTERM_VERSION:0:5} ${XTERM_VERSION:6}"
    time "$FEN2TXT" -sbrown $FEN
elif [ "$TERM_PROGRAM" = Apple_Terminal ] ; then
    img_url 3 16 "Apple Terminal $TERM_PROGRAM_VERSION"
    time "$FEN2TXT" -sbrown $FEN
else
    echo "Unknown terminal program: ${XTERM_PROGRAM:-${TERM}}"
    time "$FEN2TXT" $FEN
fi

echo Time to render while discarding output:
time "$FEN2TXT" -sbrown $FEN >/dev/null
