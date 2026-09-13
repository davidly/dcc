#include <stdio.h>
#include <stdint.h>

unsigned char minimax_wave8_guard_before = 0x5a;

#ifdef MMW26_SIGNED_BYTE
#define uint8_t int8_t
#endif
#define main minimax_wave8_original_main
#include "../ttt.c"
#undef main
#ifdef MMW26_SIGNED_BYTE
#undef uint8_t
#endif

unsigned char minimax_wave8_guard_after = 0xa5;

static int checks;
static int failures;
static unsigned long signature = 5381UL;

static void check_value(const char *name, unsigned long actual,
                        unsigned long expected)
{
    ++checks;
    signature = signature * 33UL + actual;
    if (actual != expected) {
        ++failures;
        printf("FAIL %s got=%lu expected=%lu\n",
               name, actual, expected);
    }
}

static void clear_board(void)
{
    int i;

    for (i = 0; i < 9; ++i)
        g_board[i] = PieceBlank;
}

static void check_search(int position, unsigned long expected_moves)
{
    int i;

    check_value("FindSolution", FindSolution((ttt_t)position), 0);
    check_value("moves", g_Moves, expected_moves);
    for (i = 0; i < 9; ++i)
        check_value("board", g_board[i],
                    i == position ? PieceX : PieceBlank);
    check_value("guard-before", minimax_wave8_guard_before, 0x5a);
    check_value("guard-after", minimax_wave8_guard_after, 0xa5);
}

int main(void)
{
    g_Moves = 0;
    check_search(0, 1903UL);
    check_search(1, 4361UL);
    check_search(4, 6493UL);

    clear_board();
    g_board[0] = PieceX;
    g_board[1] = PieceX;
    g_board[2] = PieceX;
    check_value("winner-x", winner_functions[2](), PieceX);

    clear_board();
    g_board[0] = PieceO;
    g_board[4] = PieceO;
    g_board[8] = PieceO;
    check_value("winner-o", winner_functions[8](), PieceO);

    clear_board();
    g_board[0] = PieceX;
    g_board[4] = PieceO;
    check_value("winner-none", winner_functions[4](), PieceBlank);
    check_value("terminal-win", SCORE_WIN, 6);
    check_value("terminal-tie", SCORE_TIE, 5);
    check_value("terminal-lose", SCORE_LOSE, 4);
    check_value("lower-bound", SCORE_MIN, 2);
    check_value("upper-bound", SCORE_MAX, 9);
    check_value("loop-bound", sizeof(g_board), 9);
    check_value("final-guard-before", minimax_wave8_guard_before, 0x5a);
    check_value("final-guard-after", minimax_wave8_guard_after, 0xa5);

    printf("MINIMAX-WAVE8 checks=%d failures=%d signature=%lu moves=%lu\n",
           checks, failures, signature, g_Moves);
    return failures != 0;
}
