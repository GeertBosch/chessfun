# chess utility bash shell functions for testing and debugging

echo "startpos - outputs the FEN for the standard starting position"
startpos() {
		echo "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
}

echo "expfen <fen> - expands a FEN placement so empty squares become '_'"
expfen() {
	if (($# != 1)) ; then
		echo "expfen <fen>"
		return 1
	fi
	set - $1
	underscores=""
	exp=${1//./_}
	shift
	for i in $(seq 8) ; do
		underscores="_$underscores"
		exp=${exp//$i/$underscores}
	done
	while [ $# -ge 1 ] ; do
			exp="$exp $1"
			shift
	done
	echo "$exp"
}

# normplmt <expanded_placement> - normalize underscore runs to FEN digits
normplmt() {
	local norm=$1 underscores="________" i
	for i in 8 7 6 5 4 3 2 1 ; do
		norm="${norm//$underscores/$i}"
		underscores="${underscores%_}"
	done
	echo "$norm"
}

echo "normfen <fen> - normalizes FEN, reversing expfen"
normfen() {
	if (($# != 1)) ; then
		echo "normfen <fen>"
		return 1
	fi
	set - $(expfen "$1")
	local norm=$(normplmt "$1")
	shift
	while [ $# -ge 1 ] ; do
		norm="$norm $1"
		shift
	done
	echo "$norm"
}

# Return the position of square $1 in an expanded FEN placement, as a 0-based index
fensqrpos() {
	echo $(( 9*$(( 8 - ${1:1:1} )) + $(( $(printf '%d' "'${1:0:1}") - 97 )) ))
}

echo "fenget <placement> <square> - returns piece at square; '_' for empty"
fenget() {
	local exp=$(expfen "$1") pos=$(fensqrpos "$2")
	echo "${exp:pos:1}"
}

echo "fenput <placement> <square> <piece> - returns placement with piece at square; '_' to clear"
fenput() {
	local exp=$(expfen "$1") pos=$(fensqrpos "$2")
	normplmt "${exp:0:$pos}$3${exp:$((pos+1))}"
}

# ispawn <piece> - true if the piece is a pawn of either color
ispawn() {
	[[ "$1" == [Pp] ]]
}

# sqrshift <square> <filedelta> <rankdelta> - square shifted by the deltas, empty if off the board
sqrshift() {
	local files=abcdefgh
	local file=$(( $(printf '%d' "'${1:0:1}") - 97 + $2 )) rank=$(( ${1:1:1} + $3 ))
	(( file < 0 || file > 7 || rank < 1 || rank > 8 )) && return 0
	echo "${files:file:1}$rank"
}

# epvictim <piece> <to> <enpassant> - square of the pawn captured en passant, empty if no such capture
epvictim() {
	ispawn "$1" && [ "$3" != "-" ] && [ "$2" == "$3" ] || return 0
	[ "$1" == "P" ] && sqrshift "$2" 0 -1 || sqrshift "$2" 0 1
}

# movecastlingrook <placement> <piece> <ucimove> - placement with the castling rook relocated, if castling
movecastlingrook() {
	local placement=$1 rookfrom rookto rook
	case "$2$3" in
	Ke1g1) rookfrom=h1 rookto=f1 rook=R ;;
	Ke1c1) rookfrom=a1 rookto=d1 rook=R ;;
	ke8g8) rookfrom=h8 rookto=f8 rook=r ;;
	ke8c8) rookfrom=a8 rookto=d8 rook=r ;;
	*) echo "$placement" ; return ;;
	esac
	placement=$(fenput "$placement" "$rookfrom" "_")
	fenput "$placement" "$rookto" "$rook"
}

# promotedpiece <piece> <promo> - the piece to put on the destination square, promoting if promo is given
promotedpiece() {
	[ -n "$2" ] || { echo "$1" ; return ; }
	[ "$1" == "P" ] || { echo "$2" ; return ; } # black promotion letters need no mapping
	case "$2" in
	q) echo Q ;; r) echo R ;; b) echo B ;; n) echo N ;;
	esac
}

# updatecastling <castling> <from> <to> - castling rights after a move touching the given squares
updatecastling() {
	local rights=$1 sqr
	for sqr in "$2" "$3" ; do
		case "$sqr" in
		e1) rights=${rights//[KQ]/} ;;
		e8) rights=${rights//[kq]/} ;;
		h1) rights=${rights//K/} ;;
		a1) rights=${rights//Q/} ;;
		h8) rights=${rights//k/} ;;
		a8) rights=${rights//q/} ;;
		esac
	done
	echo "${rights:--}"
}

# newenpassant <placement> <piece> <from> <to> - en passant square after a double pawn push that an
# enemy pawn can actually answer, '-' otherwise
newenpassant() {
	local placement=$1 piece=$2 from=$3 to=$4 dir enemy adjacent delta
	case "$piece" in
	P) dir=1 ; enemy=p ;;
	p) dir=-1 ; enemy=P ;;
	*) echo "-" ; return ;;
	esac
	(( ${to:1:1} - ${from:1:1} == 2 * dir )) || { echo "-" ; return ; }
	for delta in -1 1 ; do
		adjacent=$(sqrshift "$to" $delta 0)
		[ -n "$adjacent" ] && [ "$(fenget "$placement" "$adjacent")" == "$enemy" ] \
			&& { sqrshift "$to" 0 $((-dir)) ; return ; }
	done
	echo "-"
}

# newhalfmove <halfmove> <piece> <captured> <epvictim> - halfmove clock, reset by pawn moves and captures
newhalfmove() {
	ispawn "$2" || [ "$3" != "_" ] || [ -n "$4" ] && echo 0 || echo $(($1 + 1))
}

# applymove <fen> <ucimove> - returns the FEN after applying a single UCI move
applymove() {
	local placement active castling enpassant halfmove fullmove
	read placement active castling enpassant halfmove fullmove <<< "$1"
	local move=$2 from=${2:0:2} to=${2:2:2} promo=${2:4:1}
	local piece captured epsqr
	piece=$(fenget "$placement" "$from")
	captured=$(fenget "$placement" "$to")
	epsqr=$(epvictim "$piece" "$to" "$enpassant")

	placement=$(fenput "$placement" "$from" "_")
	[ -n "$epsqr" ] && placement=$(fenput "$placement" "$epsqr" "_")
	placement=$(movecastlingrook "$placement" "$piece" "$move")
	placement=$(fenput "$placement" "$to" "$(promotedpiece "$piece" "$promo")")

	castling=$(updatecastling "$castling" "$from" "$to")
	enpassant=$(newenpassant "$placement" "$piece" "$from" "$to")
	halfmove=$(newhalfmove "$halfmove" "$piece" "$captured" "$epsqr")
	[ "$active" == "b" ] && fullmove=$((fullmove + 1))
	[ "$active" == "w" ] && active=b || active=w

	echo "$placement $active $castling $enpassant $halfmove $fullmove"
}

# applies UCI moves directly to a FEN position
echo "applymoves <fen> <ucimove> ... - returns FEN after applying the given moves"
applymoves() {
	[ $# -eq 1 ] && [[ "$1" == *" "* ]] && set -- $1 # treat single arg with spaces as multiple args
	local fen
	if [ "$1" == "startpos" ] ; then
		fen=$(startpos)
		shift
	elif (($# >= 6)) ; then
		fen="$1 $2 $3 $4 $5 $6" # a FEN spells out its six fields as separate arguments
		shift 6
	else
		echo "applymoves <fen> <ucimove> ..."
		return 1
	fi
	[ "$1" == "moves" ] && shift

	while (($#)) ; do
		fen=$(applymove "$fen" "$1")
		shift
	done
	echo "$fen"
}

export esc="\\033["
echo "setfg [color256] - sets the terminal foreground color to the given index, or reset"
setfg() {
	if (($# < 1)) ; then
		printf "\\E[0m"
	else
		printf "\\E[38;5;$1m"
	fi
}

echo "setbg [color256] - sets the terminal background color to the given index, or reset"
setbg() {
	if (($# < 1)) ; then
		printf "\\E[49m"
	else
		printf "\\E[48;5;$1m"
	fi
}
export lg="\\E[48;5;187m" # light green background
export dg="\\E[48;5;64m" # dark green background
export lh="▌" # left half block
export rh="▐" # right half block

echo "decdhl <text> - prints text using double width/height excape sequences when supported"
decdhl() {
	if [ "$TERM_PROGRAM" = "Apple_Terminal" ] ; then
		echo -e "\033#3$*"
		echo -e "\033#4$*"
	else
		echo -e "$*"
	fi
}


echo "chessrow <bg1> <bg2> <fenrow> - prints a chess row with alternating background colors"
chessrow() {
	if (($# < 3)) ; then
		echo "chessrow <bg1> <bg2> <fenrow>"
		return 1
	fi
	bg1=$1
	bg2=$2
	row=$(echo "$3" | tr "." " ")
	for (( i=0; i<${#row}; i++ )) ; do
		ch=${row:$i:1}
		case $ch in
		"p") ch="♟" ;; "n") ch="♞" ;; "b") ch="♝" ;; "r") ch="♜" ;; "q") ch="♛" ;; "k") ch="♚" ;;
		"P") ch="♙" ;; "N") ch="♘" ;; "B") ch="♗" ;; "R") ch="♖" ;; "Q") ch="♕" ;; "K") ch="♔" ;;
		"." | '_') ch=" " ;;
		esac
		if [ $((i % 2)) == 0 ] ; then
			printf "\\E[38;5;${bg1}m$rh\\E[48;5;${bg1}m\\E[38;5;0m$ch"
		else
			printf "\\E[38;5;${bg1}m\\E[48;5;${bg2}m$lh\\E[38;5;0m$ch"
		fi
	done
	printf "\\E[49m\\E[38;5;${bg2}m$lh\\E[39m\\n"
}

echo "chessboard <fen> - prints a chess board for the given FEN piece placement"
chessboard() {
	if (($# < 1)) ; then
		echo "chessboard <fen>"
		return 1
	fi
	bg1=187
	bg2=64
	set - $(echo "$(expfen $*)" | tr '/' '\n')
	while [ $# -gt 0 ] ; do
		decdhl $(chessrow $bg1 $bg2 "$1")
		shift
		t=$bg1
		bg1=$bg2
		bg2=$t
	done
}

echo "flip <fen> - flips white and black pieces and perspective in a FEN string"
flip() {
	if (($# < 1)) ; then
		echo "flip <fen>"
		return 1
	fi
	echo -n $(echo "$1" | tr '/' '\n' | tail -r | rev) | tr ' ' '/'
	shift
	if (($# >= 1)) ; then
		case $1 in
		b|B) side=w ;;
		w|W) side=b ;;
		*) side=w ;;
		esac
		echo -n " $side"
		shift
	fi
	# flip castling rights: swap K and k, Q and q, and output uppercase before lowercase
	if (($# >= 1)) ; then
		echo -n " ${1//[^KQ]/}${1//[^kq-]/}" | tr 'KQkq' 'kqKQ'
		shift
	fi
	# flip en passant square: swap 3 and 6 ranks and reverse file order
	if (($# >= 1)) ; then
		echo -n " $1" | tr '36abcdefgh' '63hgfedcba'
		shift
	fi
	# halfmove and fullmove don't need to be flipped, just printed in order if present
	while (($# != 0)) ; do
		echo -n " $1"
		shift
	done
	echo ""
}

echo "fish <fen> [depth] - runs stockfish on the position with the given depth"
fish() {
	if (($# < 1)) ; then
		echo "fish <fen> [depth]"
		return 1
	fi
	fen=$1
	depth=9
	shift
	if (($# > 0 && $1 > 0 && $1 < 99)) ; then
		depth=$1
	fi
	echo "\
uci
position fen \"$fen\"
go depth $depth
" | while read line ; do echo "$line" ; sleep 0.1 ; done | stockfish | cat
}

echo "nnue <fen> - prints Stockfish's NNUE evaluation for the given FEN position"
nnue() {
	if (($# < 1)) ; then
		echo "nnue <fen>"
		return 1
	fi
		fen=$1
		shift
		echo "\
uci
position fen \"$fen\"
eval
" | stockfish | grep "^NNUE eval"
}

# Use perft divisions to check against perft at lower depth
echo "checkperft <fen> <depth>"
checkperft() {
	./perft "$1" $2 |
	grep -iv nodes | tr ':' ' ' |
	while read move count ; do
		echo "./perft -q \"$1\" $move $(($2 - 1)) $count" >&2
		./perft -q "$1" $move $(($2 - 1)) $count
	done |
	awk '
		/^applied move/ { move = $3 }
		/^Nodes searched:/ { print move ": " $3; total += $3 }
		NF == 2 { print }
		END { print "Nodes searched: " total }
	'
}


# Compare a perft run on a  base FEN position followed by some moves at the given depth
perftdiff() {
	if [ $# == 0 ] ; then
		echo "peftdiff fen depth"
		return 1
	elif [ $# == 1 ] ; then
		echo "set fen to startpos"
		fen="startpos"
	else
		echo "set fen to $1"
		fen="$1"
		shift
	fi
	if [ "$fen" == "startpos" ] ; then
		fen="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
	fi
	moves=""
	if [ "$1" == "moves" ] ; then
		echo "skipping $1."
		shift
	fi
	if (($# > 1)) ; then
		moves="moves"
		while (($# > 1)) ; do
			moves="$moves $1"
			shift
		done
	fi
	depth=$1
	echo "perft \"$fen\" $moves $depth"

	diff -u <(
echo "\
position fen \"$fen\" $moves
d
go perft $depth" | stockfish 2>&1 | egrep "^....(.)?:|Fen" | sort) <(
./perft-debug "$fen" $moves $depth | egrep "^....(.)?:|Fen" | sort)
}

perftnext() {
	fen="$1"
	depth="$2"
	next=$(perftdiff "$fen" $2 | egrep "^[-+][a-h][1-8][a-h][1-8]([qrbn])?:|^[+-]Fen" | head -1 | cut -c2-)
	echo "next: $next, result $?"
	if [ $? != 0 ] ; then
		echo "error $?, result $next"
		return 1
	fi
	move=$(echo "$next" | cut -d: -f1)
	if [ "$move" == "Fen" ] ; then
		echo "Incorrect Fen: $next"
		return 2
	fi
	echo "Move: $move"
	nextfen=$(apply "$fen" "$move"| egrep "Fen:" | cut -d" " -f2-)
	echo "perftnext \"$nextfen\"" $((depth - 1))

}
