# Show initial examples using plain ASCII text
# The Apple Terminal was used in the README.md.

source "$(dirname $BASH_SOURCE)/../textfun.sh"

FEN2TXT="${FEN2TXT:-$(dirname $BASH_SOURCE)/../fen2txt}"
ID=m4
IMGBASE="img/m4"

img_url() { echo "<img src=\"$IMGBASE-$1.png\" height=\"$(( $2 * 15 ))\" alt=\"$ID - $3\">" ; }

FEN=r1bqk1nr/pp1p2bp/4n3/2p1Npp1/5P2/2N1P1PP/PPP5/1RBQKB1R
# puzzle from https://lichess.org/study/Ji2GfwLC/OdxgfK8H

img_url 1 8 "Too Narrow"
"$FEN2TXT" $FEN | chess_chars | checkered_grid

img_url 2 8 "Off Center"
"$FEN2TXT" $FEN | checkered_grid | sed -e "s/[kqrbnp ]/& /gi" | chess_chars

img_url 3 8 "Too Wide"
"$FEN2TXT" $FEN | checkered_grid | sed -e "s/[kqrbnp ]/ & /gi" | chess_chars

img_url 4 8 "Just Right?"
"$FEN2TXT" $FEN | chess_chars | checkered_grid -w
