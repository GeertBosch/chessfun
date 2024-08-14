#!/bin/bash

# Load the colorfun library from the same directory as this script
source "$(dirname ${BASH_SOURCE})/colorfun.sh"

# Use ANSI escape codes to display text in double width
wide_lines() {
    while read line ; do
        printf "\\033#6$line\n"
    done
}

# Use DECDHL escape codes to display text in double height. Apple Terminal.app supports this.
large_lines() {
    while read line ; do
        printf "\\033#3$line\n"
        printf "\\033#4$line\n"
    done
}

get_cursor_pos() {
    local pos
    echo -ne "\033[6n"
    read -s -d\[ pos # discard response up to here
    read -s -d R pos # terminate the input after R
    echo -n $pos
}

# Check for proper Unicode support by ensuring a multi-byte wide character has a length of 1. The
# first argument is the multi-byte character to test and the second one is the name of the calling
# function for error messages. Both arguments are optional.
check_unicode() {
    local char="${1:-💩}"
    local prog="${2:-$FUNCNAME}"
    if [ ${#char} -ne 1 ] ; then
        echo "$prog: Your $SHELL does not appear to support UTF-8." >&2
        echo "$prog: \"$char\" should count as single character. Check your environment." >&2
        return 3
    fi
}

# Check that tr supports Unicode multi-byte characters
check_tr() {
    local char="${1:-💩}"
    local prog="${2:-$FUNCNAME}"
    if [ "$(echo x | tr x "$char")" != "$char" ] ; then
        echo "Your 'tr' command doesn't support multi-byte Unicode characters"
        return 3
    fi
}

# Substitute printable ASCII characters by their wide character Unicode counterparts
wide_chars() {
    check_unicode "Ａ" "$FUNCNAME" || return $?
    check_tr "Ａ" "$FUNCNAME" || return $?
    from="A-Za-z0-9!\"#\$%&'()*+,-./:;<=>?@[\\\\]^_\`{|}~"
    to="Ａ-Ｚａ-ｚ０-９！＂＃＄％＆＇（）＊＋，－．／：；＜＝＞？＠［＼］＾＿｀｛｜｝～"
    while read line ; do
        line="${line//─/──}" #
        echo "${line// /  }" # Replace spaces by double spaces
    done | tr "$from" "$to"
}

# Substitute letters denoting chess pieces by their Unicode counterparts
chess_chars() {
    check_unicode "♙" "$FUNCNAME" || return 1
    while read line ; do
        for ((j = 0; j < ${#line}; ++j)) ; do
            local char=${line:j:1}
            case "$char" in
                K) printf "♔" ;; k) printf "♚" ;;
                Q) printf "♕" ;; q) printf "♛" ;;
                R) printf "♖" ;; r) printf "♜" ;;
                B) printf "♗" ;; b) printf "♝" ;;
                N) printf "♘" ;; n) printf "♞" ;;
                P) printf "♙" ;; p) printf "♟" ;;
                *) printf "$char" ;;
            esac
        done
        echo
    done
}

# Given an input line, and ASCII art for the 6 chess pieces, substitute letters
# on the input denoting these chess pieces, by the corresponding ASCII art.
# Applies a white foreground color for  uppercase pieces.
# The third and fourth arguments indicate the alternating background color
pixelart_row() {
    local width=$((${#2} / 6))
    local bg1=${3:-187}
    local bg2=${4:-64}
    for ((j = 0; j < ${#1}; ++j)) ; do
        set_bg $bg1
        local char=${1:j:1}
        if [[  $char =~ [KQRBNP] ]] ; then set_fg 250 ; else set_fg 0 ; fi
        case "$char" in
            K|k) printf "${2:0*$width:$width}" ;;
            Q|q) printf "${2:1*$width:$width}" ;;
            R|r) printf "${2:2*$width:$width}" ;;
            B|b) printf "${2:3*$width:$width}" ;;
            N|n) printf "${2:4*$width:$width}" ;;
            P|p) printf "${2:5*$width:$width}" ;;
            '.') printf "%${width}s" ;;
            *) printf "$char" ;;
        esac
        local tmp=$bg1
        bg1=$bg2
        bg2=$tmp
    done
    set_bg
    echo
}

# Create chess pieces using ASCII art
pixelart() {
    local bg1=${1:-187}
    local bg2=${2:-64}

    #          ▖ ▗ ▘ ▙ ▚ ▛ ▜ ▝ ▞ ▟ ▀ ▄ ▌ ▐
    #          012345670123456701234567012345670123456701234567
    local art="   ▟▙     ▖▚▙▘▖ ▗▖▄▖▄▖▄    ▟▛     ▗█▟▙     ▄▄   "
    art="${art}  ▗▟▙▖    ▝██▛  ▝▜████▀   ██▟█   ▟█▟██▌   ▐██▌  "
    art="${art}  ▝██▘     ▐█    ▐████     ██    ▀▀██▛     ██   "
    art="${art} ▄████▄  ▄████▄ ▐██████  ▄████▄  ▄████▄  ▗████▖ "

    # The width in characters is always twice the height, as that results in
    # a square. Given that there are 6 piece definitions, find the height.
    local height=1
    while ((height * (height * 2) * 6 < ${#art})) ; do ((++height)) ; done
    local art_width=$((height * 2 * 6))

    while read line ; do
        for ((i=0; i < height; ++i)) ; do
	    pixelart_row "$line" "${art:i * art_width:art_width}" $bg1 $bg2
        done
        local tmp=$bg1
        bg1=$bg2
        bg2=$tmp
    done
}

# Given a string and cell separator, output a row for a grid
grid_row() {
    local len=${#1}
    echo -n "${1:0:1}"
    for ((i = 1; i < len; ++i)) ; do
        echo -n "$2${1:i:1}"
    done
    echo ""
}

# Make a grid from the input lines using 11 separators:
#
#   1  2  3  2  4    ┌───┬───┐
#   5  .  5  .  5    │   │   │
#   6  2  7  2  8    ├───┼───┤
#   5  .  5  .  5    │   │   │
#   9  2 10  2 11    └───┴───┘
#
# Optionally, a 12th and 13th separator may be passed to widen the cells and horizontal borders.
make_grid() {
    check_unicode "$2" "$FUNCNAME" || return 1
    read line
    line="${line//./ }" # Replace periods by spaces
    line="${line//．/　}" # Replace fullwidth periods by fullwidth spaces
    echo "$1${13}$(grid_row "${line//?/$2}" "${13}$3${13}")${13}$4"
    echo "$5${12}$(grid_row "${line}" "${12}$5${12}")${12}$5"
    local sep="$6${13}$(grid_row "${line//?/$2}" "${13}$7${13}")${13}$8"
    while read next ; do
        echo "$sep"
        next="${next//./ }" # Replace periods by spaces
        next="${next//．/　}" # Replace fullwidth periods by fullwidth spaces
        echo "$5${12}$(grid_row "${next}" "${12}$5${12}")${12}$5"
    done
    echo "$9${13}$(grid_row "${line//?/$2}" "${13}${10}${13}")${13}${11}"
}

# Break the lines into character cells and add a border around each using Unicode box drawing
# characters. Cells containing a single period ('.') are considered empty.
unicode_grid() {
    check_unicode "┼" "$FUNCNAME" || return 1
    if [ "$1" = "-w" ] ; then
        make_grid "┌" "─" "┬" "┐" "│" "├" "┼" "┤" "└" "┴" "┘" " " "─"
    else
        make_grid "┌" "─" "┬" "┐" "│" "├" "┼" "┤" "└" "┴" "┘"
    fi
}

# Break the lines into character cells and add a border around each using ASCII.
# Cells containing a single period ('.') are considered empty.
ascii_grid() {
    if [ "$1" = "-w" ] ; then
        make_grid "+" "-" "+" "+" "|" "+" "+" "+" "+" "+" "+" " " "-"
    else
        make_grid "+" "-" "+" "+" "|" "+" "+" "+" "+" "+" "+"
    fi
}


# Break the line passed as first argument into characters. The second and third arguments are ANSI
# escape sequences to switch between alternate background colors. Cells containing a single period
# ('.') are considered empty.
checkered_row() {
    local wide=
    if [ "$1" = "-w" ] ; then
        wide=$1
        shift
    fi
    local row=$1
    local bg1=${2:-187}
    local bg2=${3:-64}

    if test -n "$wide"  ; then
        lh="▌" # left half block
        rh="▐" # right half block
        check_unicode "$lh" "$FUNCNAME $wide" || return $?
        esc1=$(printf "\\033[38;5;${bg1}m$rh\\033[48;5;${bg1}m\\033[38;5;0m")
        esc2=$(printf "\\033[38;5;${bg1}m\\033[48;5;${bg2}m$lh\\033[38;5;0m")
        esc3=$(printf "\\033[49m\\033[38;5;${bg2}m$lh\\033[39m\\n");
    else
        esc1=$(printf "\\033[48;5;${bg1}m")
        esc2=$(printf "\\033[48;5;${bg2}m")
        esc3=$(printf "\\033[0m")
    fi

    row=${row//./ }  # Replace periods by spaces
    row=${row//．/　} # Replace fullwidth periods by fullwidth spaces

    for (( i=0; i<${#row}; i++ )) ; do
        echo -n "${esc1}${row:$i:1}"
        t="$esc1"
        esc1="$esc2"
        esc2="$t"
    done

    echo "$esc3"
}

# Break the lines passed passed on standard input into characters. The first and second arguments
# are ANSI color codes to switch between background colors from a 256 color palette. Cells
# containing a single period ('.') are considered empty and replaced by empty space.
checkered_grid() {
    local wide=
    if [ "$1" = "-w" ] ; then
        wide=$1
        shift
    fi

    local bg1=${1:-187}
    local bg2=${2:-64}
    while read line ; do
        checkered_row $wide "$line" "$bg1" "$bg2" || return $?

        t="$bg1"
        bg1="$bg2"
        bg2="$t"
    done
}
