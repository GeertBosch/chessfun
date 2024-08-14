# Show initial examples using plain ASCII text
# The Apple Terminal was used in the README.md.

FEN2TXT="${FEN2TXT:-$(dirname ${BASH_SOURCE})/../fen2txt}"
IMGBASE="img/m1"

img_url() { echo "<img src=\"$IMGBASE-$1.png\" height=\"$(( $2 * 15 ))\">" ; }

FEN=4rb2/3qrk2/1p1p1n2/7p/P2P4/4R2P/1BQN1P2/1K4R1
# puzzle from https://lichess.org/study/IPtfJlNl/h43IhlEz

img_url 1 8
"$FEN2TXT" -s none $FEN | sed -e 's/[.]/ /g'

img_url 2 8
"$FEN2TXT" -s none $FEN

img_url 3 8
"$FEN2TXT" -s gnu $FEN

img_url 4 17
"$FEN2TXT" -s stock $FEN

