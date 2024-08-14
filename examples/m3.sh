# Use Old Skool ANSI escape sequences for color
# The Apple Terminal was used in the README.md.
# For reproducing the README images, run this on a terminal that is not graphics capable.

FEN2TXT="${FEN2TXT:-$(dirname ${BASH_SOURCE})/../fen2txt}"
FEN2SVG="${FEN2TXT/txt/svg}"
IMGBASE="img/m3"
# This section is half scale to fit in width of the README
img_url() { echo "<img src=\"$IMGBASE-$1.png\" height=\"$(( ($2 * 15 + 1) / 2 ))\">" ; }

FEN=rn3r1k/p3qp2/bp2p2p/3pP3/P2NRQ2/1Pb2NPP/5PB1/2R3K1
# puzzle from https://lichess.org/study/rLfeXlT9/uBOsFKlm

# Heights are halved here to make it fit on screen in the README

echo "$(img_url 1 49)"
"$FEN2SVG" -t "-g96x48 -pq" $FEN  # Abuse option handling to force quarter blocks
echo "It's White's turn to move"

echo "$(img_url 2 33)"
"$FEN2TXT" -s brown -s pixelart $FEN
echo "It's White's turn to move"

echo "$(img_url 3 9)"
"$FEN2TXT" -cs gnu $FEN
echo "It's White's turn to move"
