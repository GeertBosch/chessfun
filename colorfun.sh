#!/bin/bash

# set_fg [color256] [text] - Sets the terminal foreground color to the given index, or resets.
#                           If text is specified, print text with the color and reset.
set_fg() {
    case $# in
		0) printf "\\033[0m" ;;
		1) printf "\\E[38;5;${1}m" ;;
		2) printf "\\E[38;5;${1}m$2\\033[0m" ;;
		*) printf "\\E[38;5;${1}m" ; shift ; printf "$*\\[39m" ;;
	esac
}

# set_bg [color256] [text] - Sets the terminal background color to the given index, or resets.
#                           If text is specified, print text with the color and reset.
set_bg() {
	case $# in
		0) printf "\\033[0m" ;;
		1) printf "\\E[48;5;${1}m" ;;
		2) printf "\\E[48;5;${1}m$2\\033[0m" ;;
		*) printf "\\E[48;5;${1}m" ; shift ; printf "$*\\[49m" ;;
	esac
}

show_colors() {
	local block="████" # Four full block Unicode characters (U+2588)
	if [ ${#block} -ne 4 ] ; then
		block="####" # Four hash characters to approximate a full block
	fi

	local show_foreground=
	local show_standard=1
	local show_extra=
	local show_gray=
	OPTIND=1
	while getopts "afgx" opt ; do
		case $opt in
			a) show_extra=1 ; show_gray=1 ;;
			f) show_foreground=1 ;;
			g) show_gray=1 ; show_standard= ;;
			x) show_extra=1 ; show_standard= ;;
			*) echo "unknown option\"$opt\": usage $0 -[afgx]" ; return 1 ;;
		esac
	done

	if [ -n "$show_standard" ] ; then
		printf "16 Standard Colors:"
		printf "\n%19s" "00-07  "
		set_fg 7
		for i in {0..7} ; do printf "$(set_bg $i) %2i " $i ; set_fg ; done
		if [ -n "$show_foreground" ] ; then
			printf "\n     (Foreground)  "
			for i in {0..7} ; do set_fg $i "$block" ; done
		else
			printf "\n%19s" ""
			for i in {0..7} ; do set_bg $i "    " ; done
		fi

		printf "\n%19s" "08-15  "
		set_fg 7
		for i in {8..15} ; do printf "$(set_bg $i) %2i " $i ; set_fg ; done
		set_fg 7
		if [ -n "$show_foreground" ] ; then
			printf "\n     (Foreground)  "
			for i in {8..15} ; do set_fg $i "$block" ; done ; echo
		else
			printf "\n%19s" ""
			for i in {8..15} ; do set_bg $i "    " ; set_fg ; done
		fi
		echo
	fi

	if [ -n "$show_extra" ] ; then
		printf "%-19s" "216 Extra Colors:"
		for i in 0 6 12 18 24 30 ; do printf "%-12i" $i ; done
		for i in {16..231} ; do
			if [ $(( (i - 16) % 36 )) -eq 0 ] ; then
				printf "\n%13i-%3i  " $i $(( i + 35 ))
			fi
			set_bg $i "  "
		done
		echo
	fi

	if [ -n "$show_gray" ] ; then
		printf "24 Grayscale Colors:"
		for j in 100 10 1 ; do
			case $j in
				10) printf "\n%19s" "232-255  "  ;;
				*) printf "\n%19s" "" ;;
			esac
			for i in {232..255} ; do
				if (( i < 244 )) ; then set_fg 15 ; else set_fg 0 ; fi
				set_bg $i " $(( i / j % 10 )) "
			done
		done
		echo
	fi

}