( Prove you can't win at tic-tac-toe if the opponent is competent. )
( Expected visited board positions: 6493 )

(  board: 0 1 2 )
(         3 4 5 )
(         6 7 8 )

( true global variables )
VARIABLE board 9 ALLOT
VARIABLE moves
VARIABLE recursionDepth
VARIABLE lastMove

( effectively locals in MINMAX )
VARIABLE alpha
VARIABLE beta
VARIABLE pieceMove
VARIABLE val
VARIABLE boardIndex
VARIABLE score

6 CONSTANT scoreWin
5 CONSTANT scoreTie
4 CONSTANT scoreLose
9 CONSTANT scoreMax
2 CONSTANT scoreMin
0 CONSTANT scoreInvalid

0 CONSTANT pieceBlank
1 CONSTANT pieceX
2 CONSTANT pieceO

board 0 + CONSTANT B0
board 1 + CONSTANT B1
board 2 + CONSTANT B2
board 3 + CONSTANT B3
board 4 + CONSTANT B4
board 5 + CONSTANT B5
board 6 + CONSTANT B6
board 7 + CONSTANT B7
board 8 + CONSTANT B8

( Per-position winner words: only check lines through the last-played square. )
( Returns winning piece, or pieceBlank if no winner on that square's lines.   )

: WINNER0
    B0 C@
    DUP B1 C@ = IF DUP B2 C@ = IF EXIT THEN THEN
    DUP B3 C@ = IF DUP B6 C@ = IF EXIT THEN THEN
    DUP B4 C@ = IF DUP B8 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

: WINNER1
    B1 C@
    DUP B0 C@ = IF DUP B2 C@ = IF EXIT THEN THEN
    DUP B4 C@ = IF DUP B7 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

: WINNER2
    B2 C@
    DUP B0 C@ = IF DUP B1 C@ = IF EXIT THEN THEN
    DUP B5 C@ = IF DUP B8 C@ = IF EXIT THEN THEN
    DUP B4 C@ = IF DUP B6 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

: WINNER3
    B3 C@
    DUP B4 C@ = IF DUP B5 C@ = IF EXIT THEN THEN
    DUP B0 C@ = IF DUP B6 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

: WINNER4
    B4 C@
    DUP B0 C@ = IF DUP B8 C@ = IF EXIT THEN THEN
    DUP B2 C@ = IF DUP B6 C@ = IF EXIT THEN THEN
    DUP B1 C@ = IF DUP B7 C@ = IF EXIT THEN THEN
    DUP B3 C@ = IF DUP B5 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

: WINNER5
    B5 C@
    DUP B3 C@ = IF DUP B4 C@ = IF EXIT THEN THEN
    DUP B2 C@ = IF DUP B8 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

: WINNER6
    B6 C@
    DUP B7 C@ = IF DUP B8 C@ = IF EXIT THEN THEN
    DUP B0 C@ = IF DUP B3 C@ = IF EXIT THEN THEN
    DUP B4 C@ = IF DUP B2 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

: WINNER7
    B7 C@
    DUP B6 C@ = IF DUP B8 C@ = IF EXIT THEN THEN
    DUP B1 C@ = IF DUP B4 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

: WINNER8
    B8 C@
    DUP B6 C@ = IF DUP B7 C@ = IF EXIT THEN THEN
    DUP B2 C@ = IF DUP B5 C@ = IF EXIT THEN THEN
    DUP B0 C@ = IF DUP B4 C@ = IF EXIT THEN THEN
    DROP pieceBlank ;

( Dispatch to the per-position winner word for the last-played square. )
: CALLWINNER
    lastMove @
    DUP 0 = IF DROP WINNER0 EXIT THEN
    DUP 1 = IF DROP WINNER1 EXIT THEN
    DUP 2 = IF DROP WINNER2 EXIT THEN
    DUP 3 = IF DROP WINNER3 EXIT THEN
    DUP 4 = IF DROP WINNER4 EXIT THEN
    DUP 5 = IF DROP WINNER5 EXIT THEN
    DUP 6 = IF DROP WINNER6 EXIT THEN
    DUP 7 = IF DROP WINNER7 EXIT THEN
    DROP WINNER8 ;

: dumpboard
    ." board: "
    9 0 DO
        I board + C@ .
    LOOP
    CR
    ;

: MINMAX ( stack: alpha beta )
    1 moves +!
    0 val !

    recursionDepth @ 3 > IF
        CALLWINNER
        DUP pieceBlank <> IF
            pieceX = IF scoreWin ELSE scoreLose THEN
            val !
        ELSE
            DROP
            recursionDepth @ 8 = IF scoreTie val ! THEN
        THEN
    THEN

    0 val @ = IF
        recursionDepth @ 1 AND IF
            scoreMin val !
            pieceX pieceMove !
        ELSE
            scoreMax val !
            pieceO pieceMove !
        THEN

        beta !
        alpha !
        alpha @
        beta @
        0 boardIndex !

        BEGIN boardIndex @ 9 < WHILE
            boardIndex @ board + C@ 0 = IF
                pieceMove @ board boardIndex @ + C!
                boardIndex @ lastMove !
                boardIndex @ pieceMove @ val @
                1 recursionDepth +!

                alpha @ beta @ RECURSE

                -1 recursionDepth +!
                score !
                beta ! alpha ! val ! pieceMove ! boardIndex !
                pieceBlank boardIndex @ board + C!

                pieceX pieceMove @ = IF
                    score @ scoreWin = IF
                        scoreWin val !
                        10 boardIndex !
                    ELSE
                        score @ val @ > IF
                            score @ val !
                            val @ beta @ < INVERT IF
                                10 boardIndex !
                            ELSE
                                val @ alpha @ > IF val @ alpha ! THEN
                            THEN
                        THEN
                    THEN
                ELSE
                    score @ scoreLose = IF
                        scoreLose val !
                        10 boardIndex !
                    ELSE
                        score @ val @ < IF
                            score @ val !
                            val @ alpha @ > INVERT IF
                                10 boardIndex !
                            ELSE
                                val @ beta @ < IF val @ beta ! THEN
                            THEN
                        THEN
                    THEN
                THEN
            THEN
            1 boardIndex +!
        REPEAT
    THEN

    val @
    ;

: RUNIT  ( move -- )
    pieceX
    OVER board + C!       ( make the move )
    0 recursionDepth !
    scoreMin scoreMax MINMAX

    scoreTie <> IF ." there's a bug somewhere" THEN
    DROP DROP             ( remove alpha and beta )

    pieceBlank
    SWAP board + C!       ( restore the board to the original state )
    ;

: TTT
    board 9 pieceBlank FILL

    ( all other first moves are reflections of these 3 )
    1 0 DO
        0 moves !
        0 RUNIT
        1 RUNIT
        4 RUNIT
    LOOP

    ." 1 iterations" CR
    ." moves evaluated: " moves @ . CR
;

TTT
BYE
