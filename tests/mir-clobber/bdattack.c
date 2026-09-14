/* Exhaustive exact-schedule fixture for board attack detection. */
#include <stdio.h>
#include <string.h>

#define EMPTY '.'
#define WHITE 1
#define BLACK -1

#ifdef BOARDATTACK_VOLATILE_BOARD
static volatile char board[64];
#else
static char board[64];
#endif

#ifdef BOARDATTACK_VOLATILE_DIRECTIONS
static volatile int knight_dir[8] = {
#else
static int knight_dir[8] = {
#endif
    17, 15, 10, 6, -17, -15, -10, -6
};

#ifdef BOARDATTACK_VOLATILE_DIRECTIONS
static volatile int king_dir[8] = {
#else
static int king_dir[8] = {
#endif
    1, -1, 8, -8, 9, 7, -9, -7
};

static inline int piece_side(char p)
{
    if (p >= 'A' && p <= 'Z')
        return WHITE;
    if (p >= 'a' && p <= 'z')
        return BLACK;
    return 0;
}

static inline char upiece(char p)
{
    if (p >= 'a' && p <= 'z')
        return (char)(p - 'a' + 'A');
    return p;
}

static inline int abs_i(int x)
{
    return x < 0 ? -x : x;
}

static inline int file_of(int sq)
{
    return sq & 7;
}

static inline int on_board(int sq)
{
    return sq >= 0 && sq < 64;
}

static int attacked_by_slider(int sq, int by, int dir, char a, char b)
{
    int s;
    int f0;
    int f1;
    char p;

    s = sq + dir;
    while (on_board(s)) {
        f0 = file_of(s - dir);
        f1 = file_of(s);
        if (abs_i(f1 - f0) > 1)
            break;
        p = board[s];
        if (p != EMPTY) {
            if (piece_side(p) == by) {
                p = upiece(p);
                if (p == a || p == b)
                    return 1;
            }
            return 0;
        }
        s += dir;
    }
    return 0;
}

#ifdef BOARDATTACK_RENAMED
#define board_attack_fixture board_attack_fixture_renamed
#endif

static int board_attack_fixture(int sq, int by)
{
    int f;
    int i;
    int s;
    char p;

    f = file_of(sq);

    if (by == WHITE) {
        if (f < 7 && sq >= 7 && board[sq - 7] == 'P') return 1;
        if (f > 0 && sq >= 9 && board[sq - 9] == 'P') return 1;
    } else {
        if (f > 0 && sq <= 56 && board[sq + 7] == 'p') return 1;
        if (f < 7 && sq <= 54 && board[sq + 9] == 'p') return 1;
    }

    for (i = 0; i < 8; ++i) {
        s = sq + knight_dir[i];
        if (on_board(s) && abs_i(file_of(s) - f) <= 2) {
            p = board[s];
            if (piece_side(p) == by && upiece(p) == 'N')
                return 1;
        }
    }

    if (attacked_by_slider(sq, by, 1, 'R', 'Q')) return 1;
    if (attacked_by_slider(sq, by, -1, 'R', 'Q')) return 1;
    if (attacked_by_slider(sq, by, 8, 'R', 'Q')) return 1;
    if (attacked_by_slider(sq, by, -8, 'R', 'Q')) return 1;
    if (attacked_by_slider(sq, by, 9, 'B', 'Q')) return 1;
    if (attacked_by_slider(sq, by, 7, 'B', 'Q')) return 1;
    if (attacked_by_slider(sq, by, -9, 'B', 'Q')) return 1;
    if (attacked_by_slider(sq, by, -7, 'B', 'Q')) return 1;

    for (i = 0; i < 8; ++i) {
        s = sq + king_dir[i];
        if (on_board(s) && abs_i(file_of(s) - f) <= 1) {
            p = board[s];
            if (piece_side(p) == by && upiece(p) == 'K')
                return 1;
        }
    }

#ifdef BOARDATTACK_EXTRA_CFG
    if (board[0] == 0)
        return 2;
#endif
    return 0;
}

static int reference_attack(int sq, int by)
{
    static const int knight_row[8] = {
        2, 1, -1, -2, -2, -1, 1, 2
    };
    static const int knight_col[8] = {
        1, 2, 2, 1, -1, -2, -2, -1
    };
    static const int king_row[8] = {
        0, 0, 1, -1, 1, 1, -1, -1
    };
    static const int king_col[8] = {
        1, -1, 0, 0, 1, -1, 1, -1
    };
    static const int ray_row[8] = {
        0, 0, 1, -1, 1, 1, -1, -1
    };
    static const int ray_col[8] = {
        1, -1, 0, 0, 1, -1, 1, -1
    };
    int row;
    int col;
    int item;
    int r;
    int c;
    char p;
    char wanted;

    row = sq >> 3;
    col = sq & 7;
    if (by == WHITE) {
        if (row > 0 && col < 7 && board[sq - 7] == 'P')
            return 1;
        if (row > 0 && col > 0 && board[sq - 9] == 'P')
            return 1;
    } else {
        if (row < 7 && col > 0 && board[sq + 7] == 'p')
            return 1;
        if (row < 7 && col < 7 && board[sq + 9] == 'p')
            return 1;
    }
    for (item = 0; item < 8; ++item) {
        r = row + knight_row[item];
        c = col + knight_col[item];
        if (r >= 0 && r < 8 && c >= 0 && c < 8) {
            p = board[r * 8 + c];
            if (piece_side(p) == by && upiece(p) == 'N')
                return 1;
        }
    }
    for (item = 0; item < 8; ++item) {
        r = row + ray_row[item];
        c = col + ray_col[item];
        wanted = item < 4 ? 'R' : 'B';
        while (r >= 0 && r < 8 && c >= 0 && c < 8) {
            p = board[r * 8 + c];
            if (p != EMPTY) {
                if (piece_side(p) == by &&
                    (upiece(p) == wanted || upiece(p) == 'Q'))
                    return 1;
                break;
            }
            r += ray_row[item];
            c += ray_col[item];
        }
    }
    for (item = 0; item < 8; ++item) {
        r = row + king_row[item];
        c = col + king_col[item];
        if (r >= 0 && r < 8 && c >= 0 && c < 8) {
            p = board[r * 8 + c];
            if (piece_side(p) == by && upiece(p) == 'K')
                return 1;
        }
    }
    return 0;
}

static int check_position(int square, int by, const char *pieces)
{
    int item;
    int failures;
    int expected;
    int actual;

    memset((void *)board, EMPTY, sizeof(board));
    for (item = 0; item < (unsigned char)pieces[0]; ++item)
        board[(unsigned char)pieces[item * 2 + 1]] =
            pieces[item * 2 + 2];
    expected = reference_attack(square, by);
    actual = board_attack_fixture(square, by);
    failures = actual != expected;
    return failures * 10 + actual;
}

int main(void)
{
    static const char pawn_white[] = { 1, 20, 'P' };
    static const char pawn_black[] = { 1, 34, 'p' };
    static const char knight_white[] = { 1, 44, 'N' };
    static const char rook_black[] = { 1, 24, 'r' };
    static const char bishop_white[] = { 1, 54, 'B' };
    static const char blocked_queen[] = { 2, 36, 'p', 45, 'Q' };
    static const char king_black[] = { 1, 36, 'k' };
    static const char empty[] = { 0 };
    int failures;
    int checksum;
    int result;

    failures = 0;
    checksum = 0;
#define CHECK(square, by, pieces) \
    result = check_position((square), (by), (pieces)); \
    failures += result / 10; \
    checksum = checksum * 3 + result % 10
    CHECK(27, WHITE, pawn_white);
    CHECK(27, BLACK, pawn_black);
    CHECK(28, WHITE, knight_white);
    CHECK(27, BLACK, rook_black);
    CHECK(27, WHITE, bishop_white);
    CHECK(27, WHITE, blocked_queen);
    CHECK(27, BLACK, king_black);
    CHECK(0, WHITE, empty);
#undef CHECK
    printf("board attack failures=%d checksum=%d\n", failures, checksum);
    return failures != 0;
}
