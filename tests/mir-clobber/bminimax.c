#include <stdio.h>

#define SCORE_WIN 6
#define SCORE_TIE 5
#define SCORE_LOSE 4
#define SCORE_MAX 9
#define SCORE_MIN 2

#define PIECE_X 1
#define PIECE_O 2
#define PIECE_BLANK 0

#ifdef BYTE_MINIMAX_SIGNED_BYTE
typedef signed char byte_value_t;
#else
typedef unsigned char byte_value_t;
#endif

#ifdef BYTE_MINIMAX_VOLATILE_BOARD
static volatile byte_value_t byte_board[9];
#else
static byte_value_t byte_board[9];
#endif

#ifdef BYTE_MINIMAX_VOLATILE_MOVES
static volatile unsigned long byte_moves;
#else
static unsigned long byte_moves;
#endif

static byte_value_t winner_at(int move)
{
    static const unsigned char lines[9][8] = {
        {1, 2, 3, 6, 4, 8, 255, 255},
        {0, 2, 4, 7, 255, 255, 255, 255},
        {0, 1, 5, 8, 4, 6, 255, 255},
        {4, 5, 0, 6, 255, 255, 255, 255},
        {0, 8, 2, 6, 1, 7, 3, 5},
        {3, 4, 2, 8, 255, 255, 255, 255},
        {7, 8, 0, 3, 4, 2, 255, 255},
        {6, 8, 1, 4, 255, 255, 255, 255},
        {6, 7, 2, 5, 0, 4, 255, 255}
    };
    byte_value_t piece;
    int pair;

    piece = byte_board[move];
    for (pair = 0; pair < 4 && lines[move][pair * 2] != 255; ++pair)
        if (piece == byte_board[lines[move][pair * 2]] &&
            piece == byte_board[lines[move][pair * 2 + 1]])
            return piece;
    return PIECE_BLANK;
}

#define WINNER_WRAPPER(index) \
    static byte_value_t winner_##index(void) { return winner_at(index); }

WINNER_WRAPPER(0)
WINNER_WRAPPER(1)
WINNER_WRAPPER(2)
WINNER_WRAPPER(3)
WINNER_WRAPPER(4)
WINNER_WRAPPER(5)
WINNER_WRAPPER(6)
WINNER_WRAPPER(7)
WINNER_WRAPPER(8)

typedef byte_value_t (*winner_function_t)(void);

static winner_function_t byte_winners[
#ifdef BYTE_MINIMAX_WIDE_WINNERS
    10
#else
    9
#endif
] = {
    winner_0, winner_1, winner_2, winner_3, winner_4,
    winner_5, winner_6, winner_7, winner_8
#ifdef BYTE_MINIMAX_WIDE_WINNERS
    , winner_0
#endif
};

#ifdef BYTE_MINIMAX_RENAMED
#define BYTE_MINIMAX_FUNCTION byte_minimax_renamed
#else
#define BYTE_MINIMAX_FUNCTION byte_minimax
#endif

#ifdef BYTE_MINIMAX_ALT_PHI
#define BYTE_MINIMAX_PRUNE_ENABLED (depth < 9)
#else
#define BYTE_MINIMAX_PRUNE_ENABLED 1
#endif

static byte_value_t BYTE_MINIMAX_FUNCTION(
    byte_value_t alpha, byte_value_t beta,
    byte_value_t depth, byte_value_t move)
{
    byte_value_t value;
    byte_value_t piece;
#ifdef BYTE_MINIMAX_WIDE_INDEX
    unsigned int p;
#else
    byte_value_t p;
#endif
#ifdef BYTE_MINIMAX_VOLATILE_SCORE
    volatile byte_value_t score;
#else
    byte_value_t score;
#endif

#ifdef BYTE_MINIMAX_EXTRA_CFG
    if (depth == 255)
        return SCORE_TIE;
#endif
    byte_moves++;

    if (depth >= 4) {
        p = byte_winners[move]();
        if (PIECE_BLANK != p) {
            if (PIECE_X == p)
                return SCORE_WIN;
            return SCORE_LOSE;
        }
        if (8 == depth)
            return SCORE_TIE;
    }

    if (depth & 1) {
        value = SCORE_MIN;
        piece = PIECE_X;
    } else {
        value = SCORE_MAX;
        piece = PIECE_O;
    }

    for (p = 0; p < 9; p++) {
        if (PIECE_BLANK == byte_board[p]) {
            byte_board[p] = piece;
            score = BYTE_MINIMAX_FUNCTION(alpha, beta, depth + 1, p);
            byte_board[p] = PIECE_BLANK;

            if (depth & 1) {
                if (BYTE_MINIMAX_PRUNE_ENABLED && SCORE_WIN == score)
                    return SCORE_WIN;
                if (score > value) {
                    value = score;
                    if (1) {
                        if (value >= beta)
                            return value;
                        if (value > alpha)
                            alpha = value;
                    }
                }
            } else {
                if (BYTE_MINIMAX_PRUNE_ENABLED && SCORE_LOSE == score)
                    return SCORE_LOSE;
                if (score < value) {
                    value = score;
                    if (1) {
                        if (value <= alpha)
                            return value;
                        if (value < beta)
                            beta = value;
                    }
                }
            }
        }
    }
    return value;
}

static unsigned char reference_board[9];
static unsigned long reference_moves;

static unsigned char reference_winner(int move)
{
    static const unsigned char lines[9][8] = {
        {1, 2, 3, 6, 4, 8, 255, 255},
        {0, 2, 4, 7, 255, 255, 255, 255},
        {0, 1, 5, 8, 4, 6, 255, 255},
        {4, 5, 0, 6, 255, 255, 255, 255},
        {0, 8, 2, 6, 1, 7, 3, 5},
        {3, 4, 2, 8, 255, 255, 255, 255},
        {7, 8, 0, 3, 4, 2, 255, 255},
        {6, 8, 1, 4, 255, 255, 255, 255},
        {6, 7, 2, 5, 0, 4, 255, 255}
    };
    unsigned char piece;
    int pair;

    piece = reference_board[move];
    for (pair = 0; pair < 4 && lines[move][pair * 2] != 255; ++pair)
        if (piece == reference_board[lines[move][pair * 2]] &&
            piece == reference_board[lines[move][pair * 2 + 1]])
            return piece;
    return PIECE_BLANK;
}

static unsigned char reference_minimax(
    unsigned char alpha, unsigned char beta,
    unsigned char depth, unsigned char move)
{
    unsigned char value;
    unsigned char piece;
    unsigned char index;
    unsigned char score;

    ++reference_moves;
    if (depth >= 4) {
        index = reference_winner(move);
        if (index != PIECE_BLANK)
            return index == PIECE_X ? SCORE_WIN : SCORE_LOSE;
        if (depth == 8)
            return SCORE_TIE;
    }
    if (depth & 1) {
        value = SCORE_MIN;
        piece = PIECE_X;
    } else {
        value = SCORE_MAX;
        piece = PIECE_O;
    }
    for (index = 0; index < 9; ++index) {
        if (reference_board[index] == PIECE_BLANK) {
            reference_board[index] = piece;
            score = reference_minimax(alpha, beta, depth + 1, index);
            reference_board[index] = PIECE_BLANK;
            if (depth & 1) {
                if (score == SCORE_WIN)
                    return SCORE_WIN;
                if (score > value) {
                    value = score;
                    if (value >= beta)
                        return value;
                    if (value > alpha)
                        alpha = value;
                }
            } else {
                if (score == SCORE_LOSE)
                    return SCORE_LOSE;
                if (score < value) {
                    value = score;
                    if (value <= alpha)
                        return value;
                    if (value < beta)
                        beta = value;
                }
            }
        }
    }
    return value;
}

static int failures;
static int checks;
static unsigned long signature = 5381UL;

static void check_value(unsigned long actual, unsigned long expected)
{
    ++checks;
    signature = signature * 33UL + actual;
    if (actual != expected)
        ++failures;
}

static void check_position(int initial)
{
    unsigned char actual;
    unsigned char expected;
    int index;

    for (index = 0; index < 9; ++index) {
        byte_board[index] = PIECE_BLANK;
        reference_board[index] = PIECE_BLANK;
    }
    byte_board[initial] = PIECE_X;
    reference_board[initial] = PIECE_X;
    byte_moves = 0;
    reference_moves = 0;
    actual = BYTE_MINIMAX_FUNCTION(
        SCORE_MIN, SCORE_MAX, 0, (byte_value_t)initial);
    expected = reference_minimax(SCORE_MIN, SCORE_MAX, 0, initial);
    check_value(actual, expected);
    check_value(byte_moves, reference_moves);
    for (index = 0; index < 9; ++index)
        check_value(byte_board[index], reference_board[index]);
}

int main(void)
{
    check_position(0);
    check_position(1);
    check_position(4);
    printf(
        "byte minimax checks=%d failures=%d signature=%lu\n",
        checks, failures, signature);
    return failures != 0;
}
