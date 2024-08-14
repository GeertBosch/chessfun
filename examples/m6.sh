# Show final SVG rendered examples in the brown, green and paper styles.
# Requires bitmap capable terminal. VSCode was used in the README.md.

FEN2SVG="${FEN2SVG:-$(dirname ${BASH_SOURCE})/../fen2svg}"

ID=m6
IMGBASE=img/$ID

img_url() { echo "<img src=\"$IMGBASE-$1.png\" height=\"$(( $2 * 15 ))\" alt=\"$ID - $3\">" ; }

FEN=r4rk1/p7/bpn1ppqp/3pP3/P2Nn1Q1/1Pb1RNPP/5PB1/3R2K1
# puzzle from https://lichess.org/study/0uBy1QsD/7VCNBuNl

img_url 1 8 "(preview)"
time timg -g16x8 chessboard.svg

img_url 2 10 brown
time "$FEN2SVG" -tg32x16 -s brown $FEN

img_url 3 10 green
time "$FEN2SVG" -tg32x16 -s green $FEN

img_url 4 10 paper
time "$FEN2SVG" -tg32x16 -s paper $FEN
