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

# applies UCI moves directly to a FEN position
echo "applymoves <fen> <ucimove> ... - returns FEN after applying the given moves"
applymoves() {
	[ $# -eq 1 ] && [[ "$1" == *" "* ]] && set -- $1 # treat single arg with spaces as multiple args
	if (($# < 2)) ; then
		echo "applymoves <fen> <ucimove> ..."
		return 1
	fi
	local fen=$1
	shift
	if [ "$fen" == "startpos" ] ; then
		fen="$(startpos)"
	fi
	[ "$1" == "moves" ] && shift

	local placement active castling enpassant halfmove fullmove
	read placement active castling enpassant halfmove fullmove <<< "$fen"

	while (($#)) ; do
		local move=$1 ; shift
		local from=${move:0:2} to=${move:2:2} promo=${move:4:1}

		local piece captured
		piece=$(fenget "$placement" "$from")
		captured=$(fenget "$placement" "$to")

		# Detect en passant capture: pawn moves to the en passant square
		local ep_capture=0
		if [ "$enpassant" != "-" ] && [ "$to" == "$enpassant" ] ; then
			if [ "$piece" == "P" ] || [ "$piece" == "p" ] ; then
				ep_capture=1
			fi
		fi

		# Remove piece from source square
		placement=$(fenput "$placement" "$from" "_")

		# En passant: remove the captured pawn behind the to-square
		if [ $ep_capture -eq 1 ] ; then
			local ep_rank=${to:1:1}
			if [ "$piece" == "P" ] ; then
				placement=$(fenput "$placement" "${to:0:1}$((ep_rank - 1))" "_")
			else
				placement=$(fenput "$placement" "${to:0:1}$((ep_rank + 1))" "_")
			fi
		fi

		# Castling: implicitly move the rook
		if [ "$piece" == "K" ] ; then
			case "$move" in
				e1g1) placement=$(fenput "$placement" h1 _)
					  placement=$(fenput "$placement" f1 R) ;;
				e1c1) placement=$(fenput "$placement" a1 _)
					  placement=$(fenput "$placement" d1 R) ;;
			esac
		elif [ "$piece" == "k" ] ; then
			case "$move" in
				e8g8) placement=$(fenput "$placement" h8 _)
					  placement=$(fenput "$placement" f8 r) ;;
				e8c8) placement=$(fenput "$placement" a8 _)
					  placement=$(fenput "$placement" d8 r) ;;
			esac
		fi

		# Place piece at destination, promoting if specified
		if [ -n "$promo" ] ; then
			local promo_piece
			if [ "$active" == "w" ] ; then
				case "$promo" in
				q) promo_piece=Q ;; r) promo_piece=R ;;
				b) promo_piece=B ;; n) promo_piece=N ;;
				esac
			else
				promo_piece=$promo
			fi
			placement=$(fenput "$placement" "$to" "$promo_piece")
		else
			placement=$(fenput "$placement" "$to" "$piece")
		fi

		# Update castling rights based on king/rook moves and rook captures
		[[ "$from" == "e1" ]] && castling="${castling//K/}" && castling="${castling//Q/}"
		[[ "$from" == "h1" || "$to" == "h1" ]] && castling="${castling//K/}"
		[[ "$from" == "a1" || "$to" == "a1" ]] && castling="${castling//Q/}"
		[[ "$from" == "e8" ]] && castling="${castling//k/}" && castling="${castling//q/}"
		[[ "$from" == "h8" || "$to" == "h8" ]] && castling="${castling//k/}"
		[[ "$from" == "a8" || "$to" == "a8" ]] && castling="${castling//q/}"
		[ -z "$castling" ] && castling="-"

		# Set en passant square on double pawn push only if enemy pawn can capture
		enpassant="-"
		local to_file="${to:0:1}" to_rank="${to:1:1}"
		if [[ "$piece" == "P" && "${from:1:1}" == "2" && "$to_rank" == "4" ]] || \
		   [[ "$piece" == "p" && "${from:1:1}" == "7" && "$to_rank" == "5" ]] ; then
			local lf="" rf=""
			case $to_file in
			a) rf="b" ;;
			b) lf="a" ; rf="c" ;; c) lf="b" ; rf="d" ;;
			d) lf="c" ; rf="e" ;; e) lf="d" ; rf="f" ;;
			f) lf="e" ; rf="g" ;; g) lf="f" ; rf="h" ;;
			h) lf="g" ;;
			esac
			local enemy_pawn lp="" rp=""
			[ "$piece" == "P" ] && enemy_pawn="p" || enemy_pawn="P"
			[ -n "$lf" ] && lp=$(fenget "$placement" "${lf}${to_rank}")
			[ -n "$rf" ] && rp=$(fenget "$placement" "${rf}${to_rank}")
			if [ "$lp" == "$enemy_pawn" ] || [ "$rp" == "$enemy_pawn" ] ; then
				[ "$piece" == "P" ] && enpassant="${to_file}3" || enpassant="${to_file}6"
			fi
		fi

		# Halfmove clock: reset on pawn move or capture
		if [ "$piece" == "P" ] || [ "$piece" == "p" ] || \
		   [ "$captured" != "_" ] || [ $ep_capture -eq 1 ] ; then
			halfmove=0
		else
			halfmove=$((halfmove + 1))
		fi

		# Fullmove counter: increment after black's move
		[ "$active" == "b" ] && fullmove=$((fullmove + 1))

		# Toggle active side
		if [ "$active" == "w" ] ; then active="b" ; else active="w" ; fi
	done

	echo "$placement $active $castling $enpassant $halfmove $fullmove"
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
