/* tchess.c - small C89 chess regression/stress test for CP/M-like systems */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define EMPTY '.'
#define WHITE 1
#define BLACK -1
#define MAXMOVES 128 // 218 is possible. with -p:5 for a game it got to 63.
#define MAXPLY 5
#define INF 30000
#define MF_EP 1
#define MF_CA 2
#define CR_WK 1
#define CR_WQ 2
#define CR_BK 4
#define CR_BQ 8

typedef struct {
    int8_t from;
    int8_t to;
    char piece;
    char capt;
    char prom;
    char flag;
    int8_t oldep;
    char oldcr;
} Move;

static char board[64];
static int side;
static int maxply;
static Move moves[MAXPLY + 1][MAXMOVES];
static int movecnt[MAXPLY + 1];
static Move best_root;
static Move tmp_moves[MAXPLY + 1][MAXMOVES];
static int bk_ply;
static int bk_ok;
static int epsq;
static char crght;

static char start_board[65] =
    "RNBQKBNR"
    "PPPPPPPP"
    "........"
    "........"
    "........"
    "........"
    "pppppppp"
    "rnbqkbnr";

/* minimal CP/M-friendly replacements */

static int xtolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');

    return c;
}

static int xstrncmp(const char *a, const char *b, int n)
{
    unsigned char ca;
    unsigned char cb;

    while (n > 0) {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;

        if (ca != cb)
            return (int)ca - (int)cb;

        if (ca == 0)
            return 0;

        ++a;
        ++b;
        --n;
    }

    return 0;
}

static inline int piece_side(char p)
{
    if (p >= 'A' && p <= 'Z') return WHITE;
    if (p >= 'a' && p <= 'z') return BLACK;
    return 0;
}

static inline char upiece(char p)
{
    if (p >= 'a' && p <= 'z') return (char)(p - 'a' + 'A');
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

static inline int rank_of(int sq)
{
    return sq >> 3;
}

static inline int on_board(int sq)
{
    return sq >= 0 && sq < 64;
}

static void init_board(void)
{
    int i;

    for (i = 0; i < 64; ++i)
        board[i] = start_board[i];

    side = WHITE;
    epsq = -1;
    crght = CR_WK | CR_WQ | CR_BK | CR_BQ;
}

static void pr_board(void)
{
    int r;
    int f;
    int sq;

    printf("\n");
    for (r = 7; r >= 0; --r) {
        printf("%d ", r + 1);
        for (f = 0; f < 8; ++f) {
            sq = r * 8 + f;
            printf("%c ", board[sq]);
        }
        printf("\n");
    }
    printf("  a b c d e f g h\n");
    printf("%s to move\n", side == WHITE ? "white" : "black");
}

static int sq_from_text(char f, char r)
{
    if (f < 'a' || f > 'h') return -1;
    if (r < '1' || r > '8') return -1;
    return (r - '1') * 8 + (f - 'a');
}

static void m_to_text(Move *m, char *s)
{
    s[0] = (char)('a' + file_of(m->from));
    s[1] = (char)('1' + rank_of(m->from));
    s[2] = (char)('a' + file_of(m->to));
    s[3] = (char)('1' + rank_of(m->to));
    s[4] = 0;

    if (m->prom != 0) {
        s[4] = (char)xtolower(m->prom);
        s[5] = 0;
    }
}

static void add_flag_move(int ply, int from, int to, char prom, char flag)
{
    Move *m;

    if (movecnt[ply] >= MAXMOVES) return;

    m = &moves[ply][movecnt[ply]++];
    m->from = from;
    m->to = to;
    m->piece = board[from];
    if (flag & MF_EP)
        m->capt = piece_side(board[from]) == WHITE ? 'p' : 'P';
    else
        m->capt = board[to];
    m->prom = prom;
    m->flag = flag;
    m->oldep = 0;
    m->oldcr = 0;
}

static void add_move(int ply, int from, int to, char prom)
{
    add_flag_move(ply, from, to, prom, 0);
}

static void copy_move(Move *d, Move *s)
{
    d->from = s->from;
    d->to = s->to;
    d->piece = s->piece;
    d->capt = s->capt;
    d->prom = s->prom;
    d->flag = s->flag;
    d->oldep = s->oldep;
    d->oldcr = s->oldcr;
}

static void make_move(Move *m)
{
    int csq;

    m->oldep = epsq;
    m->oldcr = crght;
    epsq = -1;

    if (m->piece == 'K') crght = crght & ~(CR_WK | CR_WQ);
    if (m->piece == 'k') crght = crght & ~(CR_BK | CR_BQ);
    if (m->from == 0 || m->to == 0) crght = crght & ~CR_WQ;
    if (m->from == 7 || m->to == 7) crght = crght & ~CR_WK;
    if (m->from == 56 || m->to == 56) crght = crght & ~CR_BQ;
    if (m->from == 63 || m->to == 63) crght = crght & ~CR_BK;

    board[m->to] = m->prom ? m->prom : m->piece;
    board[m->from] = EMPTY;

    if (m->flag & MF_EP) {
        csq = piece_side(m->piece) == WHITE ? m->to - 8 : m->to + 8;
        board[csq] = EMPTY;
    }

    if (m->flag & MF_CA) {
        if (m->to == 6) {
            board[5] = board[7];
            board[7] = EMPTY;
        } else if (m->to == 2) {
            board[3] = board[0];
            board[0] = EMPTY;
        } else if (m->to == 62) {
            board[61] = board[63];
            board[63] = EMPTY;
        } else if (m->to == 58) {
            board[59] = board[56];
            board[56] = EMPTY;
        }
    }

    if (upiece(m->piece) == 'P' && abs_i(m->to - m->from) == 16)
        epsq = piece_side(m->piece) == WHITE ? m->from + 8 : m->from - 8;

    side = -side;
}

static void undo_move(Move *m)
{
    int csq;

    side = -side;
    epsq = m->oldep;
    crght = m->oldcr;

    board[m->from] = m->piece;
    if (m->flag & MF_EP)
        board[m->to] = EMPTY;
    else
        board[m->to] = m->capt;

    if (m->flag & MF_EP) {
        csq = piece_side(m->piece) == WHITE ? m->to - 8 : m->to + 8;
        board[csq] = m->capt;
    }

    if (m->flag & MF_CA) {
        if (m->to == 6) {
            board[7] = board[5];
            board[5] = EMPTY;
        } else if (m->to == 2) {
            board[0] = board[3];
            board[3] = EMPTY;
        } else if (m->to == 62) {
            board[63] = board[61];
            board[61] = EMPTY;
        } else if (m->to == 58) {
            board[56] = board[59];
            board[59] = EMPTY;
        }
    }
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

static int knight_dir[8] = { 17, 15, 10, 6, -17, -15, -10, -6 };
static int king_dir[8] = { 1, -1, 8, -8, 9, 7, -9, -7 };

static int is_attacked(int sq, int by)
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

    return 0;
}

static int in_check(int sde)
{
    int i;
    char k;

    k = sde == WHITE ? 'K' : 'k';

    for (i = 0; i < 64; ++i) {
        if (board[i] == k)
            return is_attacked(i, -sde);
    }

    return 1;
}

static void gen_slide(int ply, int from, int dir)
{
    int s;
    int f0;
    int f1;
    int ps;

    ps = piece_side(board[from]);
    s = from + dir;

    while (on_board(s)) {
        f0 = file_of(s - dir);
        f1 = file_of(s);

        if (abs_i(f1 - f0) > 1)
            break;

        if (board[s] == EMPTY) {
            add_move(ply, from, s, 0);
        } else {
            if (piece_side(board[s]) == -ps)
                add_move(ply, from, s, 0);
            break;
        }

        s += dir;
    }
}

static void gen_pseudo(int ply)
{
    int i;
    int pside;
    int r;
    int f;
    int to;
    char p;
    char up;

    movecnt[ply] = 0;

    for (i = 0; i < 64; ++i) {
        p = board[i];
        pside = piece_side(p);

        if (pside != side)
            continue;

        up = upiece(p);
        r = rank_of(i);
        f = file_of(i);

        if (up == 'P') {
            if (pside == WHITE) {
                to = i + 8;
                if (to < 64 && board[to] == EMPTY) {
                    add_move(ply, i, to, rank_of(to) == 7 ? 'Q' : 0);
                    if (r == 1 && board[i + 16] == EMPTY)
                        add_move(ply, i, i + 16, 0);
                }
                if (f > 0) {
                    to = i + 7;
                    if (to < 64 && piece_side(board[to]) == BLACK)
                        add_move(ply, i, to, rank_of(to) == 7 ? 'Q' : 0);
                    else if (to == epsq)
                        add_flag_move(ply, i, to, 0, MF_EP);
                }
                if (f < 7) {
                    to = i + 9;
                    if (to < 64 && piece_side(board[to]) == BLACK)
                        add_move(ply, i, to, rank_of(to) == 7 ? 'Q' : 0);
                    else if (to == epsq)
                        add_flag_move(ply, i, to, 0, MF_EP);
                }
            } else {
                to = i - 8;
                if (to >= 0 && board[to] == EMPTY) {
                    add_move(ply, i, to, rank_of(to) == 0 ? 'q' : 0);
                    if (r == 6 && board[i - 16] == EMPTY)
                        add_move(ply, i, i - 16, 0);
                }
                if (f > 0) {
                    to = i - 9;
                    if (to >= 0 && piece_side(board[to]) == WHITE)
                        add_move(ply, i, to, rank_of(to) == 0 ? 'q' : 0);
                    else if (to == epsq)
                        add_flag_move(ply, i, to, 0, MF_EP);
                }
                if (f < 7) {
                    to = i - 7;
                    if (to >= 0 && piece_side(board[to]) == WHITE)
                        add_move(ply, i, to, rank_of(to) == 0 ? 'q' : 0);
                    else if (to == epsq)
                        add_flag_move(ply, i, to, 0, MF_EP);
                }
            }
        } else if (up == 'N') {
            int n;

            for (n = 0; n < 8; ++n) {
                to = i + knight_dir[n];
                if (on_board(to) && abs_i(file_of(to) - f) <= 2) {
                    if (piece_side(board[to]) != pside)
                        add_move(ply, i, to, 0);
                }
            }
        } else if (up == 'B') {
            gen_slide(ply, i, 9);
            gen_slide(ply, i, 7);
            gen_slide(ply, i, -9);
            gen_slide(ply, i, -7);
        } else if (up == 'R') {
            gen_slide(ply, i, 1);
            gen_slide(ply, i, -1);
            gen_slide(ply, i, 8);
            gen_slide(ply, i, -8);
        } else if (up == 'Q') {
            gen_slide(ply, i, 1);
            gen_slide(ply, i, -1);
            gen_slide(ply, i, 8);
            gen_slide(ply, i, -8);
            gen_slide(ply, i, 9);
            gen_slide(ply, i, 7);
            gen_slide(ply, i, -9);
            gen_slide(ply, i, -7);
        } else if (up == 'K') {
            int n;

            for (n = 0; n < 8; ++n) {
                to = i + king_dir[n];
                if (on_board(to) && abs_i(file_of(to) - f) <= 1) {
                    if (piece_side(board[to]) != pside)
                        add_move(ply, i, to, 0);
                }
            }

            if (pside == WHITE && i == 4 && !in_check(WHITE)) {
                if ((crght & CR_WK) &&
                    board[5] == EMPTY && board[6] == EMPTY &&
                    !is_attacked(5, BLACK) && !is_attacked(6, BLACK))
                    add_flag_move(ply, 4, 6, 0, MF_CA);
                if ((crght & CR_WQ) &&
                    board[1] == EMPTY && board[2] == EMPTY && board[3] == EMPTY &&
                    !is_attacked(3, BLACK) && !is_attacked(2, BLACK))
                    add_flag_move(ply, 4, 2, 0, MF_CA);
            } else if (pside == BLACK && i == 60 && !in_check(BLACK)) {
                if ((crght & CR_BK) &&
                    board[61] == EMPTY && board[62] == EMPTY &&
                    !is_attacked(61, WHITE) && !is_attacked(62, WHITE))
                    add_flag_move(ply, 60, 62, 0, MF_CA);
                if ((crght & CR_BQ) &&
                    board[57] == EMPTY && board[58] == EMPTY && board[59] == EMPTY &&
                    !is_attacked(59, WHITE) && !is_attacked(58, WHITE))
                    add_flag_move(ply, 60, 58, 0, MF_CA);
            }
        }
    }
}

static void gen_legal(int ply)
{
    int i;
    int n;
    int save_side;

    gen_pseudo(ply);

    n = movecnt[ply];
    for (i = 0; i < n; ++i)
        copy_move(&tmp_moves[ply][i], &moves[ply][i]);

    movecnt[ply] = 0;
    save_side = side;

    for (i = 0; i < n; ++i) {
        make_move(&tmp_moves[ply][i]);
        if (!in_check(save_side)) {
            copy_move(&moves[ply][movecnt[ply]], &tmp_moves[ply][i]);
            movecnt[ply] = movecnt[ply] + 1;
        }
        undo_move(&tmp_moves[ply][i]);
    }
}

static int value_piece(char p)
{
    switch (upiece(p)) {
    case 'P': return 100;
    case 'N': return 320;
    case 'B': return 330;
    case 'R': return 500;
    case 'Q': return 900;
    case 'K': return 20000;
    }
    return 0;
}

static int center_score(int sq)
{
    int f;
    int r;
    int df;
    int dr;

    f = file_of(sq);
    r = rank_of(sq);
    df = abs_i(f * 2 - 7);
    dr = abs_i(r * 2 - 7);

    return 14 - df - dr;
}

static int positional_value(char p, int sq)
{
    int r;
    int c;
    int v;
    char u;

    u = upiece(p);
    r = rank_of(sq);
    if (piece_side(p) == BLACK)
        r = 7 - r;

    c = center_score(sq);
    v = 0;

    if (u == 'P') {
        v = r * 5 + c;
    } else if (u == 'N') {
        v = c * 8;
        if (r > 0) v += 20;
    } else if (u == 'B') {
        v = c * 4;
        if (r > 0) v += 15;
    } else if (u == 'Q') {
        v = c;
    } else if (u == 'R') {
        v = 0;
    } else if (u == 'K') {
        v = -c * 4;
        if (r > 0) v -= 80;
    }

    return v;
}

static int evaluate(void)
{
    int i;
    int v;
    int total;

    total = 0;

    for (i = 0; i < 64; ++i) {
        if (board[i] != EMPTY) {
            v = value_piece(board[i]) + positional_value(board[i], i);
            if (piece_side(board[i]) == WHITE)
                total += v;
            else if (piece_side(board[i]) == BLACK)
                total -= v;
        }
    }

    return side == WHITE ? total : -total;
}

static int m_tiebreak(Move *m)
{
    int s;
    int r;
    char p;
    char u;

    s = 0;
    p = m->piece;
    u = upiece(p);

    if (m->capt != EMPTY)
        s += 1000 + value_piece(m->capt) - value_piece(p) / 16;

    if (m->flag & MF_CA)
        s += 250;

    if (m->flag & MF_EP)
        s += 120;

    if (m->prom != 0)
        s += 900;

    if (u == 'P') {
        r = rank_of(m->to);
        if (piece_side(p) == BLACK) r = 7 - r;
        s += r * 8 + center_score(m->to);
    } else if (u == 'N') {
        s += 120 + center_score(m->to) * 6;
        if (m->from == 1 || m->from == 6 || m->from == 57 || m->from == 62)
            s += 40;
    } else if (u == 'B') {
        s += 80 + center_score(m->to) * 4;
        if (m->from == 2 || m->from == 5 || m->from == 58 || m->from == 61)
            s += 30;
    } else if (u == 'Q') {
        s += center_score(m->to);
    } else if (u == 'R') {
        s -= 120;
    } else if (u == 'K') {
        s -= 200;
    }

    return s;
}

static int search(int depth, int ply, int alpha, int beta)
{
    uint8_t i;
    int score;
    int best;
    int oldside;
    int ord;
    int best_ord;

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
        score = -search(depth - 1, ply + 1, -beta, -alpha);
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

static int parse_move(char *s, Move *out)
{
    int from;
    int to;
    char prom;

    while (*s == ' ' || *s == '\t')
        ++s;

    if (strlen(s) < 4)
        return 0;

    from = sq_from_text((char)xtolower(s[0]), s[1]);
    to = sq_from_text((char)xtolower(s[2]), s[3]);

    if (from < 0 || to < 0)
        return 0;

    prom = 0;
    if (s[4] == 'q' || s[4] == 'Q')
        prom = side == WHITE ? 'Q' : 'q';

    out->from = from;
    out->to = to;
    out->piece = board[from];
    out->capt = board[to];
    out->prom = prom;

    return 1;
}

static int same_move(Move *a, Move *b)
{
    return a->from == b->from &&
           a->to == b->to &&
           (a->prom == b->prom || b->prom == 0);
}


static char *bk_text_at(int n)
{
    switch (n) {
    case 0: return "e2e4";
    case 1: return "e7e5";
    case 2: return "g1f3";
    case 3: return "b8c6";
    case 4: return "f1b5";
    case 5: return "a7a6";
    case 6: return "b5a4";
    case 7: return "g8f6";
    case 8: return "d2d3";
    case 9: return "f8e7";
    case 10: return "c2c3";
    case 11: return "d7d6";
    case 12: return "b1d2";
    case 13: return "c8g4";
    case 14: return "e1g1";
    case 15: return "e8g8";
    default: return 0;
    }
}

static int find_legal_text_move(char *txt, Move *m)
{
    Move want;
    int i;

    if (!parse_move(txt, &want))
        return 0;

    gen_legal(0);

    for (i = 0; i < movecnt[0]; ++i) {
        if (same_move(&moves[0][i], &want)) {
            copy_move(m, &moves[0][i]);
            return 1;
        }
    }

    return 0;
}

static int ch_bk_move(Move *m)
{
    char *txt;
    char out[8];

    if (!bk_ok)
        return 0;

    txt = bk_text_at(bk_ply);
    if (txt == 0)
        return 0;

    if (!find_legal_text_move(txt, m)) {
        bk_ok = 0;
        return 0;
    }

    m_to_text(m, out);
    printf("bk plays %s\n", out);
    return 1;
}

static void note_bk_move(Move *m)
{
    char *txt;
    Move want;

    if (!bk_ok) {
        bk_ply = bk_ply + 1;
        return;
    }

    txt = bk_text_at(bk_ply);
    if (txt == 0) {
        bk_ok = 0;
        bk_ply = bk_ply + 1;
        return;
    }

    if (!parse_move(txt, &want) || !same_move(m, &want))
        bk_ok = 0;

    bk_ply = bk_ply + 1;
}

static int get_human_move(Move *m)
{
    char line[80];
    Move in;
    int i;

    for (;;) {
        printf("move: ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
            return 0;

        if (line[0] == 'q')
            return 0;

        if (!parse_move(line, &in)) {
            printf("bad move format; use e2e4\n");
            continue;
        }

        gen_legal(0);

        for (i = 0; i < movecnt[0]; ++i) {
            if (same_move(&moves[0][i], &in)) {
                copy_move(m, &moves[0][i]);
                return 1;
            }
        }

        printf("illegal move\n");
    }
}

static int ch_computer_move(Move *m)
{
    char txt[8];
    int score;

    if (ch_bk_move(m))
        return 1;

    gen_legal(0);
    if (movecnt[0] == 0)
        return 0;

    copy_move(&best_root, &moves[0][0]);
    score = search(maxply, 0, -INF, INF);
    copy_move(m, &best_root);

    m_to_text(m, txt);
    printf("computer plays %s score %d\n", txt, score);

    return 1;
}

static void usage(void)
{
    printf("usage: tchess [-C] [-P:X]\n");
    printf( "    -C   -- computer plays itself\n" );
    printf( "    -P:X -- X is the # of plies 1-%u, default is 2.\n", MAXPLY );
    exit( 1 );
}


extern char __bssb;
extern char __bsse;
extern char __heap_start;

int main(int argc, char **argv)
{
    int computer;
    int i;
    int move_no;
    Move m;

#if 0
    char *b = &__bssb;
    char *e = &__bsse;
    char *h = &__heap_start;
    printf( "heap_start %x, bss %x, bss end %x\n", h, b, e );
#endif

    computer = 0;
    maxply = 2;
    bk_ply = 0;
    bk_ok = 1;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-C") == 0 || strcmp(argv[i], "-c") == 0) {
            computer = 1;
        } else if (xstrncmp(argv[i], "-P:", 3) == 0 || xstrncmp(argv[i], "-p:", 3) == 0) {
            maxply = atoi(argv[i] + 3);
            if (maxply < 1) maxply = 1;
            if (maxply > MAXPLY) usage();
        } else {
            usage();
        }
    }

    init_board();
    pr_board();

    move_no = 1;

    for (;;) {
        if (computer || side == BLACK) {
            if (!ch_computer_move(&m))
                break;
        } else {
            if (!get_human_move(&m))
                break;
        }

        note_bk_move(&m);
        make_move(&m);
        pr_board();

        ++move_no;
        if (move_no > 200) {
            printf("move limit reached\n");
            break;
        }
    }

    if (in_check(side))
        printf("checkmate or no legal move while in check\n");
    else
        printf("stalemate, quit, or no legal move\n");

    return 0;
}
