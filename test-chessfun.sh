#!/bin/bash
# Regression tests for chessfun.sh - run ./test-chessfun.sh, exits non-zero on failure.
# The expected FENs for applymoves were cross-checked against stockfish.

cd "$(dirname "$0")" || exit 1
source ./chessfun.sh > /dev/null

passed=0 failed=0

# check <expected> <command> [arg ...] - runs the command, comparing its output (stderr included)
check() {
	local want=$1 got ; shift
	got=$("$@" 2>&1)
	if [ "$got" == "$want" ] ; then
		passed=$((passed + 1))
	else
		failed=$((failed + 1))
		echo "FAIL: $*"
		echo "  expected: $want"
		echo "  actual  : $got"
	fi
}

# checktrue <command> [arg ...] - expects the command to succeed without output
checktrue() {
	check "" "$@"
	"$@" > /dev/null 2>&1 && return
	failed=$((failed + 1))
	echo "FAIL: $* - expected success, got exit $?"
}

# checkfalse <command> [arg ...] - expects the command to fail without output
checkfalse() {
	check "" "$@"
	"$@" > /dev/null 2>&1 || return
	failed=$((failed + 1))
	echo "FAIL: $* - expected failure, got success"
}

# plain <command> [arg ...] - runs the command, stripping terminal escape sequences from its output
plain() {
	"$@" | LC_ALL=C sed $'s/\033\[[0-9;]*m//g'
}

echo "== error reporting"
check "oops" error oops
check "" eval '(error oops) 2> /dev/null'	# the message goes to stderr, not stdout
check "1" eval '(error oops) 2> /dev/null ; echo $?'
check "3" eval '(error 3 oops) 2> /dev/null ; echo $?'
check "42" eval '(error 42) 2>&1'	# a lone number is the message, not the code

echo "== placement encoding"
check "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" startpos
check "rnbqkbnr/pppppppp/________/________/________/________/PPPPPPPP/RNBQKBNR w KQkq - 0 1" \
	expfen "$(startpos)"
check "8/8/8/8/8/8/8/8" normplmt "________/________/________/________/________/________/________/________"
check "4k3/8/8/8/8/8/8/4K3 w - - 0 1" normfen "____k___/________/________/________/________/________/________/____K___ w - - 0 1"
check "$(startpos)" normfen "$(expfen "$(startpos)")"
check "expfen <fen>" expfen
check "normfen <fen>" normfen

echo "== square access"
check "0" fensqrpos a8
check "70" fensqrpos h1
check "40" fensqrpos e4
check "R" fenget "$(startpos)" a1
check "q" fenget "$(startpos)" d8
check "_" fenget "$(startpos)" e4
check "8/8/8/8/4Q3/8/8/8" fenput "8/8/8/8/8/8/8/8" e4 Q
check "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN1 w KQkq - 0 1" fenput "$(startpos)" h1 _
check "R" fenget "$(fenput "8/8/8/8/8/8/8/8" a1 R)" a1

echo "== move helpers"
check "f3" sqrshift e4 1 -1
check "" sqrshift a1 -1 0
check "" sqrshift h8 0 1
check "a8" sqrshift a1 0 7
checktrue ispawn P
checktrue ispawn p
checkfalse ispawn K
checkfalse ispawn _
check "d5" epvictim P d6 d6		# white captures the black pawn on d5
check "a4" epvictim p a3 a3		# black captures the white pawn on a4
check "" epvictim N d6 d6		# only pawns capture en passant
check "" epvictim P d5 -		# no en passant square
check "" epvictim P d5 d6		# not moving to the en passant square
check "Q" promotedpiece P q
check "N" promotedpiece P n
check "q" promotedpiece p q
check "P" promotedpiece P ""		# no promotion: the pawn itself
check "kq" updatecastling KQkq e1 e2	# white king moves
check "KQ" updatecastling KQkq e8 d8	# black king moves
check "Kk" updatecastling KQkq a8 a1	# both queen rooks leave or are captured
check "q" updatecastling Kq h1 h8
check "-" updatecastling K h1 g1		# last right lost
check "KQkq" updatecastling KQkq g1 f3	# an unrelated move keeps all rights
check "d6" newenpassant "rnbqkbnr/pp2pppp/8/2ppP3/8/8/PPPP1PPP/RNBQKBNR" p d7 d5
check "-" newenpassant "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR" P e2 e4 # no enemy pawn to answer
check "-" newenpassant "8/8/8/8/8/8/8/8" N g1 f3	# not a pawn move
check "-" newenpassant "4k3/8/8/8/8/4P3/8/4K3" P e2 e3	# single push
check "6" newhalfmove 5 N _ ""		# quiet move
check "0" newhalfmove 5 P _ ""		# pawn move
check "0" newhalfmove 5 N q ""		# capture
check "0" newhalfmove 5 N _ d5		# en passant capture
check "r3k2r/8/8/8/8/8/8/R3KR2" movecastlingrook "r3k2r/8/8/8/8/8/8/R3K2R" K e1g1
check "r3k2r/8/8/8/8/8/8/3RK2R" movecastlingrook "r3k2r/8/8/8/8/8/8/R3K2R" K e1c1
check "r3kr2/8/8/8/8/8/8/R3K2R" movecastlingrook "r3k2r/8/8/8/8/8/8/R3K2R" k e8g8
check "3rk2r/8/8/8/8/8/8/R3K2R" movecastlingrook "r3k2r/8/8/8/8/8/8/R3K2R" k e8c8
check "r3k2r/8/8/8/8/8/8/R3K2R" movecastlingrook "r3k2r/8/8/8/8/8/8/R3K2R" N g1f3
check "r3k2r/8/8/8/8/8/8/R3K2R" movecastlingrook "r3k2r/8/8/8/8/8/8/R3K2R" k e1g1 # not black's king move

echo "== applymove"
check "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1" applymove "$(startpos)" e2e4
check "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1" applymove "$(startpos)" g1f3
check "rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2" \
	applymove "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1" g8f6

echo "== applymoves: games from the starting position"
check "r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 5 4" \
	applymoves startpos e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 e1g1
check "2kr2nr/pp1n1ppp/2p1p3/q7/1b1P1B2/P1N2Q1P/1PP1BPP1/R3K2R w KQ - 1 11" \
	applymoves startpos moves e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 c7c6 g1f3 c8g4 c1f4 e7e6 h2h3 \
		g4f3 d1f3 f8b4 f1e2 b8d7 a2a3 e8c8
check "rnbqkbnr/pp3ppp/3p4/2p5/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 4" \
	applymoves startpos e2e4 c7c5 e4e5 d7d5 e5d6 e7d6	# en passant capture
check "rnbq1rk1/ppp1bppp/4pn2/3p2B1/2PP4/2N1P3/PP3PPP/R2QKBNR w KQ - 1 6" \
	applymoves startpos d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8
check "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1" applymoves startpos e2e4
check "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3" \
	applymoves "startpos e2e4 e7e5 g1f3 b8c6"

echo "== applymoves: explicit FEN positions"
check "2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2" applymoves "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1 e1g1 e8c8"
check "r4rk1/8/8/8/8/8/8/2KR3R w - - 2 2" applymoves "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1 e1c1 e8g8"
check "R3k2r/8/8/8/8/8/8/4K2R b Kk - 0 1" applymoves "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1 a1a8"
check "Q7/7k/8/8/8/8/6K1/7q w - - 0 2" applymoves "8/P6k/8/8/8/8/6Kp/8 w - - 0 1 a7a8q h2h1q"
check "2N5/7k/8/8/8/8/6K1/5b2 w - - 0 2" applymoves "8/2P4k/8/8/8/8/5pK1/8 w - - 0 1 c7c8n f2f1b"
check "4k3/8/8/8/1P6/8/8/4K3 b - - 0 12" applymoves "4k3/8/8/8/8/8/1P6/4K3 w - - 5 12 b2b4"
check "4k3/8/8/8/1P6/p7/8/4K3 w - - 0 2" applymoves "4k3/8/8/8/pP6/8/8/4K3 b - - 0 1 a4a3"
check "rnbqkbnr/ppp1p1pp/5P2/3p4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3" \
	applymoves "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3 e5f6"
check "4k3/8/1P6/8/8/8/8/4K3 b - - 0 2" applymoves "4k3/8/8/1pP5/8/8/8/4K3 w - b6 0 2 c5b6"
check "8/8/8/8/8/p7/8/4K2k w - - 0 2" applymoves "8/8/8/8/1p6/8/P7/4K2k w - - 0 1 a2a4 b4a3"
check "4k3/8/8/8/8/8/8/4K3 w - - 0 1" applymoves "4k3/8/8/8/8/8/8/4K3 w - - 0 1 moves"
check "4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1" applymoves 4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1

echo "== applymoves: usage"
check "applymoves <fen> <ucimove> ..." applymoves
check "$(startpos)" applymoves startpos	# no moves: the position is returned unchanged
(applymoves) > /dev/null 2>&1
[ $? -eq 1 ] || { failed=$((failed + 1)) ; echo "FAIL: applymoves without arguments should exit 1" ; }

echo "== flip"
check "RNBKQBNR/PPPPPPPP/8/8/8/8/pppppppp/rnbkqbnr" flip "$(startpos | cut -d' ' -f1)"
check "RNBKQBNR/PPPPPPPP/8/8/8/8/pppppppp/rnbkqbnr w kqK d3 3 7" \
	flip rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQk e6 3 7
check "3K4/8/8/8/8/8/8/3k4" flip 4k3/8/8/8/8/8/8/4K3	# flip mirrors files as well as ranks
check "flip <fen>" flip

echo "== board rendering"
check "▐♜▌♞▐♝▌♛▐♚▌♝▐♞▌♜▌" plain chessrow 187 64 rnbqkbnr
check "▐ ▌ ▐ ▌ ▐ ▌ ▐ ▌ ▌" plain chessrow 187 64 "________"
check "chessrow <bg1> <bg2> <fenrow>" chessrow
check "8" eval 'chessboard rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR | wc -l | tr -d " "'
check "chessboard <fen>" chessboard

echo
echo "passed: $passed, failed: $failed"
[ $failed -eq 0 ]
