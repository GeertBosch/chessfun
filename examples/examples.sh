#!/bin/bash
ID=x
IMGBASE="img/$ID"
img_url() { echo "<img src=\"$IMGBASE-$1.png\" height=\"$(( $2 * 15 ))\" alt=\"$3\">" ; }

# Source the colorfun library from the parent directory of this script
dir="$(dirname ${BASH_SOURCE})"
source "$(dirname ${BASH_SOURCE})/../colorfun.sh"

"$dir/m1.sh"

img_url  1 5
echo '"Hello, World!" - These 13 character wide strings do not line up at all'
echo '"This stinks💩!" - These 13 character wide strings do not line up at all'
echo '"ＡＡＡ" - Three full-width letters A'
echo '"AAAAAA" - Six regular letters A'
echo '"♙♘♗♖♕♔" - Six chess pieces, if only these were Emoji.'

"$dir/m2.sh"

img_url 2a 4 "16 Standard Colors"
show_colors

img_url 2b 6 "216 Extra Colors"
show_colors -x

img_url 2c 3 "24 Grayscale Colors"
show_colors -g

"$dir/m3.sh"

"$dir/m4.sh"

"$dir/m5.sh"

"$dir/m6.sh"
