/* Dedicated semantic controls for the flagged-record append schedule. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef F24_SMALL_CAPACITY
#define F24_CAPACITY 127
#else
#define F24_CAPACITY 128
#endif

#ifdef F24_MASK_TWO
#define F24_SPECIAL_FLAG 2
#else
#define F24_SPECIAL_FLAG 1
#endif

#ifdef F24_UNSIGNED_FROM
typedef uint8_t f24_square;
#else
typedef int8_t f24_square;
#endif

typedef struct {
    f24_square from;
    int8_t to;
    char piece;
#ifdef F24_RECORD_PADDING
    char padding;
#endif
    char capt;
    char prom;
    char flag;
    int8_t oldep;
    char oldcr;
} F24Move;

#ifdef F24_UNSIGNED_COUNTS
typedef unsigned int f24_count;
#else
typedef int f24_count;
#endif

#ifdef F24_VOLATILE_COUNTS
static volatile f24_count move_count[3];
#else
static f24_count move_count[3];
#endif

#ifdef F24_VOLATILE_RECORDS
static volatile F24Move move_rows[3][F24_CAPACITY];
#define F24_MOVE_QUAL volatile
#else
static F24Move move_rows[3][F24_CAPACITY];
#define F24_MOVE_QUAL
#endif

#ifdef F24_VOLATILE_BOARD
static volatile char squares[64];
#else
static char squares[64];
#endif

#ifdef F24_CLASSIFY_UNSIGNED_ARG
#define F24_CLASSIFY_ARG unsigned char
#else
#define F24_CLASSIFY_ARG char
#endif

#ifdef F24_CLASSIFY_LONG_RETURN
static long classify_piece(F24_CLASSIFY_ARG piece)
#elif defined(F24_CLASSIFY_UNSIGNED_RETURN)
static unsigned int classify_piece(F24_CLASSIFY_ARG piece)
#elif defined(F24_CLASSIFY_VARIADIC)
static int classify_piece(F24_CLASSIFY_ARG piece, ...)
#elif defined(F24_CLASSIFY_EXTRA_ARG)
static int classify_piece(F24_CLASSIFY_ARG piece, int ignored)
#else
static int classify_piece(F24_CLASSIFY_ARG piece)
#endif
{
#ifdef F24_CLASSIFY_EXTRA_ARG
    (void)ignored;
#endif
    if (piece >= 'A' && piece <= 'Z')
        return 1;
    if (piece >= 'a' && piece <= 'z')
        return -1;
    return 0;
}

#undef F24_CLASSIFY_ARG

static int equivalent_piece_class(char piece)
{
    if (piece >= 'A' && piece <= 'Z')
        return 1;
    if (piece >= 'a' && piece <= 'z')
        return -1;
    return 0;
}

#ifdef F24_INSERT_CALL
static void observe_append(void)
{
}
#endif

#ifdef F24_RENAMED_LOCALS
#define F24_PLY depth
#define F24_FROM origin
#define F24_TO destination
#define F24_PROM promotion
#define F24_FLAG attributes
#define F24_MOVE entry
#else
#define F24_PLY ply
#define F24_FROM from
#define F24_TO to
#define F24_PROM prom
#define F24_FLAG flag
#define F24_MOVE m
#endif

#ifdef F24_RETURN_INT
static int append_flagged(
#else
static void append_flagged(
#endif
    int F24_PLY, int F24_FROM, int F24_TO,
    char F24_PROM, char F24_FLAG)
{
    F24_MOVE_QUAL F24Move *F24_MOVE;

    if (move_count[F24_PLY] >= F24_CAPACITY)
#ifdef F24_RETURN_INT
        return 0;
#else
        return;
#endif

#ifdef F24_INSERT_CALL
    observe_append();
#endif
    F24_MOVE = &move_rows[F24_PLY][move_count[F24_PLY]++];
    F24_MOVE->from = F24_FROM;
    F24_MOVE->to = F24_TO;
    F24_MOVE->piece = squares[F24_FROM];
    if (F24_FLAG & F24_SPECIAL_FLAG)
#ifdef F24_ALT_CLASSIFY
        F24_MOVE->capt =
            equivalent_piece_class(squares[F24_FROM]) == 1 ? 'p' : 'P';
#elif defined(F24_CLASSIFY_EXTRA_ARG)
        F24_MOVE->capt =
            classify_piece(squares[F24_FROM], 0) == 1 ? 'p' : 'P';
#else
        F24_MOVE->capt =
            classify_piece(squares[F24_FROM]) == 1 ? 'p' : 'P';
#endif
    else
#ifdef F24_CAPTURE_FROM
        F24_MOVE->capt = squares[F24_FROM];
#else
        F24_MOVE->capt = squares[F24_TO];
#endif
    F24_MOVE->prom = F24_PROM;
    F24_MOVE->flag = F24_FLAG;
#ifdef F24_NONZERO_HISTORY
    F24_MOVE->oldep = -1;
    F24_MOVE->oldcr = 3;
#else
    F24_MOVE->oldep = 0;
    F24_MOVE->oldcr = 0;
#endif
#ifdef F24_RETURN_INT
    return 0;
#endif
}

#undef F24_PLY
#undef F24_FROM
#undef F24_TO
#undef F24_PROM
#undef F24_FLAG
#undef F24_MOVE

static int failures;

static void check_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        printf(
            "frec24 %s actual=%d expected=%d\n",
            name, actual, expected);
        failures++;
    }
}

int main(void)
{
    F24Move *first;
    F24Move *second;
    F24Move *third;
    F24Move sentinel;

    memset((void *)move_count, 0, sizeof(move_count));
    memset((void *)move_rows, 0x5a, sizeof(move_rows));
    memset((void *)squares, '.', sizeof(squares));
    squares[5] = 'P';
    squares[6] = 'p';
    squares[7] = 'Q';
    squares[8] = 'n';

    append_flagged(0, 5, 7, 'R', 0);
    append_flagged(0, 5, 8, 0, F24_SPECIAL_FLAG);
    append_flagged(0, 6, 7, 'q', F24_SPECIAL_FLAG);
    first = (F24Move *)&move_rows[0][0];
    second = (F24Move *)&move_rows[0][1];
    third = (F24Move *)&move_rows[0][2];

    check_int("count", move_count[0], 3);
    check_int("first-from", first->from, 5);
    check_int("first-to", first->to, 7);
    check_int("first-piece", first->piece, 'P');
#ifdef F24_CAPTURE_FROM
    check_int("first-capt", first->capt, 'P');
#else
    check_int("first-capt", first->capt, 'Q');
#endif
    check_int("first-prom", first->prom, 'R');
    check_int("first-flag", first->flag, 0);
#ifdef F24_NONZERO_HISTORY
    check_int("first-oldep", first->oldep, -1);
    check_int("first-oldcr", first->oldcr, 3);
#else
    check_int("first-oldep", first->oldep, 0);
    check_int("first-oldcr", first->oldcr, 0);
#endif
    check_int("white-capt", second->capt, 'p');
    check_int("black-capt", third->capt, 'P');

    memset(&sentinel, 0x36, sizeof(sentinel));
    move_count[1] = F24_CAPACITY;
    move_rows[1][F24_CAPACITY - 1] = sentinel;
    append_flagged(1, 5, 7, 'B', F24_SPECIAL_FLAG);
    check_int("full-count", move_count[1], F24_CAPACITY);
    check_int(
        "full-guard",
        memcmp(
            (const void *)&move_rows[1][F24_CAPACITY - 1],
            &sentinel, sizeof(sentinel)),
        0);

    printf("frec24 oracle failures=%d\n", failures);
    return failures != 0;
}
