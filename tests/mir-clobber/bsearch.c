#include <stdint.h>
#include <stdio.h>

#ifdef BOARDSEARCH_SHORT_MOVES
#define MAXMOVES 127
#else
#define MAXMOVES 128
#endif
#define MAXPLY 5
#define INF 30000

typedef struct {
    int8_t from;
    int8_t to;
    char piece;
    char capt;
    char prom;
    char flag;
    int8_t oldep;
    char oldcr;
#ifdef BOARDSEARCH_WIDE_MOVE
    char extra;
#endif
} Move;

#ifdef BOARDSEARCH_VOLATILE_SIDE
static volatile int side;
#else
static int side;
#endif
#ifdef BOARDSEARCH_VOLATILE_MOVES
static volatile Move moves[MAXPLY + 1][MAXMOVES];
#else
static Move moves[MAXPLY + 1][MAXMOVES];
#endif
#ifdef BOARDSEARCH_UNSIGNED_COUNTS
static unsigned int movecnt[MAXPLY + 1];
#elif defined(BOARDSEARCH_VOLATILE_COUNTS)
static volatile int movecnt[MAXPLY + 1];
#else
static int movecnt[MAXPLY + 1];
#endif
#ifdef BOARDSEARCH_VOLATILE_BEST
static volatile Move best_root;
#else
static Move best_root;
#endif
static int position_score;

static int evaluate(void)
{
    return side * position_score;
}

static void gen_legal(int ply)
{
    movecnt[ply] = ply < 2 ? 2 : 0;
    moves[ply][0].from = (int8_t)(ply * 2 + 1);
    moves[ply][0].to = 1;
    moves[ply][1].from = (int8_t)(ply * 2 + 2);
    moves[ply][1].to = (int8_t)(ply == 0 ? 20 : 3);
}

#ifdef BOARDSEARCH_UNSIGNED_CHECK
static unsigned int in_check(int current_side)
#else
static int in_check(int current_side)
#endif
{
    return current_side == 99;
}

static void make_move(Move *move)
{
    position_score += move->to;
    side = -side;
}

static void undo_move(Move *move)
{
    side = -side;
    position_score -= move->to;
}

#ifdef BOARDSEARCH_UNSIGNED_ORDER
static unsigned int m_tiebreak(Move *move)
#else
static int m_tiebreak(Move *move)
#endif
{
    return move->from;
}

static void copy_move(Move *destination, Move *source)
{
    *destination = *source;
}

#ifdef BOARDSEARCH_RENAMED
#define BOARD_SEARCH_FUNCTION board_search_renamed
#else
#define BOARD_SEARCH_FUNCTION board_search
#endif

#ifdef BOARDSEARCH_UNSIGNED_PARAMETERS
typedef unsigned int board_search_parameter_t;
#else
typedef int board_search_parameter_t;
#endif

#ifdef BOARDSEARCH_UNSIGNED_RETURN
static unsigned int BOARD_SEARCH_FUNCTION(
#else
static int BOARD_SEARCH_FUNCTION(
#endif
    board_search_parameter_t depth,
    board_search_parameter_t ply,
    board_search_parameter_t alpha,
    board_search_parameter_t beta)
{
#ifdef BOARDSEARCH_VOLATILE_INDEX
    volatile uint8_t i;
#elif defined(BOARDSEARCH_SIGNED_INDEX)
    int8_t i;
#else
    uint8_t i;
#endif
#ifdef BOARDSEARCH_VOLATILE_SCORE
    volatile int score;
#else
    int score;
#endif
    int best;
    int oldside;
    int ord;
    int best_ord;

#ifdef BOARDSEARCH_EXTRA_CFG
    if (depth > 1000)
        return 123;
#endif
    if (depth == 0)
        return evaluate();

    gen_legal(ply);

    if (movecnt[ply] == 0) {
        if (in_check(side))
            return -20000 + ply;
        return 0;
    }

    best = -INF;
    best_ord = -INF;
    oldside = side;

    for (i = 0; i < movecnt[ply]; ++i) {
        make_move(&moves[ply][i]);
        score = -BOARD_SEARCH_FUNCTION(depth - 1, ply + 1, -beta, -alpha);
        undo_move(&moves[ply][i]);

        ord = 0;
        if (ply == 0)
            ord = m_tiebreak(&moves[ply][i]);

        if (score > best || (ply == 0 && score == best && ord > best_ord)) {
            best = score;
            best_ord = ord;
            if (ply == 0)
                copy_move(&best_root, &moves[ply][i]);
        }

        if (score > alpha)
            alpha = score;

        if (alpha >= beta)
            break;

        side = oldside;
    }

    return best;
}

int main(void)
{
    int failures;
    int score;

    failures = 0;
    side = 1;
    position_score = 0;
    score = BOARD_SEARCH_FUNCTION(2, 0, -INF, INF);
    if (score != 21)
        ++failures;
    if (best_root.from != 2 || best_root.to != 20)
        ++failures;
    if (side != 1 || position_score != 0)
        ++failures;
    if (BOARD_SEARCH_FUNCTION(1, 2, -INF, INF) != 0)
        ++failures;
    if (BOARD_SEARCH_FUNCTION(0, 0, -INF, INF) != 0)
        ++failures;
    printf(
        "board search failures=%d score=%d best=%d,%d\n",
        failures, score, best_root.from, best_root.to);
    return failures != 0;
}
