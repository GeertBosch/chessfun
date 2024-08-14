# Show initial examples using plain ASCII text
# The Apple Terminal was used in the README.md.

FEN2TXT="${FEN2TXT:-$(dirname ${BASH_SOURCE})/../fen2txt}"
IMGBASE="img/m2"

img_url() { echo "<img src=\"$IMGBASE-$1.png\" height=\"$(( $2 * 15 ))\">" ; }

FEN=r2k1b1r/p1ppq2p/np3np1/5p2/1PPP4/P3PQ2/3N1PPP/R1B1K2R
# puzzle from https://lichess.org/study/dmMcyRUf/DkAJoeoh

img_url 1 17
"$FEN2TXT" -s boxed $FEN

img_url 2 17
"$FEN2TXT" -cs boxed $FEN

img_url 3 8
"$FEN2TXT" -cs gnu $FEN
