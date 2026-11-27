#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#define ABPrune true
#define WinLosePrune true
#define SCORE_WIN 6
#define SCORE_TIE 5
#define SCORE_LOSE  4
#define SCORE_MAX 9
#define SCORE_MIN 2
#define DefaultIterations 1

#define PieceX 1
#define PieceO 2
#define PieceBlank 0

int g_Iterations = DefaultIterations;

typedef uint8_t ttt_t;

ttt_t g_board[ 9 ];

#if true

ttt_t pos0func( void )
{
    ttt_t x = g_board[0];

    if ( ( x == g_board[1] && x == g_board[2] ) ||
         ( x == g_board[3] && x == g_board[6] ) ||
         ( x == g_board[4] && x == g_board[8] ) )
        return x;
    return PieceBlank;
}

ttt_t pos1func( void )
{
    ttt_t x = g_board[1];

    if ( ( x == g_board[0] && x == g_board[2] ) ||
         ( x == g_board[4] && x == g_board[7] ) )
        return x;
    return PieceBlank;
}

ttt_t pos2func( void )
{
    ttt_t x = g_board[2];

    if ( ( x == g_board[0] && x == g_board[1] ) ||
         ( x == g_board[5] && x == g_board[8] ) ||
         ( x == g_board[4] && x == g_board[6] ) )
        return x;
    return PieceBlank;
}

ttt_t pos3func( void )
{
    ttt_t x = g_board[3];

    if ( ( x == g_board[4] && x == g_board[5] ) ||
         ( x == g_board[0] && x == g_board[6] ) )
        return x;
    return PieceBlank;
}

ttt_t pos4func( void )
{
    ttt_t x = g_board[4];

    if ( ( x == g_board[0] && x == g_board[8] ) ||
         ( x == g_board[2] && x == g_board[6] ) ||
         ( x == g_board[1] && x == g_board[7] ) ||
         ( x == g_board[3] && x == g_board[5] ) )
        return x;
    return PieceBlank;
}

ttt_t pos5func( void )
{
    ttt_t x = g_board[5];

    if ( ( x == g_board[3] && x == g_board[4] ) ||
         ( x == g_board[2] && x == g_board[8] ) )
        return x;
    return PieceBlank;
}

ttt_t pos6func( void )
{
    ttt_t x = g_board[6];

    if ( ( x == g_board[7] && x == g_board[8] ) ||
         ( x == g_board[0] && x == g_board[3] ) ||
         ( x == g_board[4] && x == g_board[2] ) )
        return x;
    return PieceBlank;
}

ttt_t pos7func( void )
{
    ttt_t x = g_board[7];

    if ( ( x == g_board[6] && x == g_board[8] ) ||
         ( x == g_board[1] && x == g_board[4] ) )
        return x;
    return PieceBlank;
}

ttt_t pos8func( void )
{
    ttt_t x = g_board[8];

    if ( ( x == g_board[6] && x == g_board[7] ) ||
         ( x == g_board[2] && x == g_board[5] ) ||
         ( x == g_board[0] && x == g_board[4] ) )
        return x;
    return PieceBlank;
}

typedef ttt_t (*pfunc_t)( void );

pfunc_t winner_functions[9] =
{
    pos0func,
    pos1func,
    pos2func,
    pos3func,
    pos4func,
    pos5func,
    pos6func,
    pos7func,
    pos8func,
};

#else

ttt_t LookForWinner()
{
    ttt_t p = g_board[0];
    if ( PieceBlank != p )
    {
        if ( p == g_board[1] && p == g_board[2] )
            return p;

        if ( p == g_board[3] && p == g_board[6] )
            return p;
    }

    p = g_board[3];
    if ( PieceBlank != p && p == g_board[4] && p == g_board[5] )
        return p;

    p = g_board[6];
    if ( PieceBlank != p && p == g_board[7] && p == g_board[8] )
        return p;

    p = g_board[1];
    if ( PieceBlank != p && p == g_board[4] && p == g_board[7] )
        return p;

    p = g_board[2];
    if ( PieceBlank != p && p == g_board[5] && p == g_board[8] )
        return p;

    p = g_board[4];
    if ( PieceBlank != p )
    {
        if ( ( p == g_board[0] ) && ( p == g_board[8] ) )
            return p;

        if ( ( p == g_board[2] ) && ( p == g_board[6] ) )
            return p;
    }

    return PieceBlank;
} /*LookForWinner*/

#endif

uint32_t g_Moves = 0;

ttt_t MinMax( ttt_t alpha, ttt_t beta, ttt_t depth, ttt_t move )
{
    ttt_t value, pieceMove, p, score;

    //printf("d=%d m=%d a=%d b=%d\n", depth, move, alpha, beta);

    g_Moves++;

    if ( depth >= 4 )
    {
        #if true
            p = winner_functions[ move ]();
        #else
            p = LookForWinner();
        #endif       

        if ( PieceBlank != p )
        {
            if ( PieceX == p )
                return SCORE_WIN;

            return SCORE_LOSE;
        }

        if ( 8 == depth )
            return SCORE_TIE;
    }

    if ( depth & 1 )
    {
        value = SCORE_MIN;
        pieceMove = PieceX;
    }
    else
    {
        value = SCORE_MAX;
        pieceMove = PieceO;
    }

    for ( p = 0; p < 9; p++ )
    {
        if ( PieceBlank == g_board[ p ] )
        {
            g_board[p] = pieceMove;
            score = MinMax( alpha, beta, depth + 1, p );
            g_board[p] = PieceBlank;

            if ( depth & 1 )
            {
                if ( WinLosePrune && SCORE_WIN == score )
                    return SCORE_WIN;

                if ( score > value )
                {
                    value = score;

                    if ( ABPrune )
                    {
                        if ( value >= beta )
                            return value;
                        if ( value > alpha )
                            alpha = value;
                    }
                }
            }
            else
            {
                if ( WinLosePrune && SCORE_LOSE == score )
                    return SCORE_LOSE;

                if ( score < value )
                {
                    value = score;

                    if ( ABPrune )
                    {
                        if ( value <= alpha )
                            return value;
                        if ( value < beta )
                            beta = value;
                    }
                }
            }
        }
    }

    return value;
}  /*MinMax*/

int FindSolution( ttt_t position )
{
    size_t i;
    for ( i = 0; i < 9; i++ )
        g_board[ i ] = PieceBlank;
    g_board[ position ] = PieceX;
    MinMax( SCORE_MIN, SCORE_MAX, 0, position );
    
    return 0;
} /*FindSolution*/

void ttt( void )
{
    FindSolution( 0 );
    FindSolution( 1 );
    FindSolution( 4 );
} //ttt

extern int main( int argc, char * argv[] )
{
    int times;

    if ( 2 == argc )
        g_Iterations = atoi( argv[1] );

    for ( times = 0; times < g_Iterations; times++ )
        ttt();

    printf( "%lu moves\n", g_Moves );
    printf( "%d iterations\n", g_Iterations );
    fflush(stdout);
    return 0;
} //main


