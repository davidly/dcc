/* Exhaustive exact-schedule fixture for the arrow path runner. */
#include <stdio.h>

#define ROOM_COUNT 20
#define TUNNELS 3
#define LOCATIONS 6

#define PLAYER 0
#define WUMPUS 1

#define CONTINUE_GAME 0
#define WIN_GAME 1
#define LOSE_GAME 2

typedef struct Game {
    int location[LOCATIONS];
    int initial[LOCATIONS];
    int arrows;
    int cpu;
} Game;

static int cave[ROOM_COUNT + 1][TUNNELS] = {
    0,  0,  0,
    2,  5,  8,
    1,  3, 10,
    2,  4, 12,
    3,  5, 14,
    1,  4,  6,
    5,  7, 15,
    6,  8, 17,
    1,  7,  9,
    8, 10, 18,
    2,  9, 11,
   10, 12, 19,
    3, 11, 13,
   12, 14, 20,
    4, 13, 15,
    6, 14, 16,
   15, 17, 20,
    7, 16, 18,
    9, 17, 19,
   11, 18, 20,
   13, 16, 19
};

static int random_values[8];
static int random_count;
static int random_cursor;
static int wake_result;
static int wake_calls;

static int adjacent_room(int from, int to)
{
    int index;

    for (index = 0; index < TUNNELS; ++index)
        if (cave[from][index] == to)
            return 1;
    return 0;
}

static int random_index(int limit)
{
    int value;

    value = random_values[random_cursor++];
    if (value < 0)
        value = -value;
    return limit > 0 ? value % limit : 0;
}

static int flush_output(void)
{
    fflush(stdout);
    return 0;
}

static int wake_game(Game *game)
{
    ++wake_calls;
    game->cpu += 1;
    return wake_result;
}

#ifdef ARROWPATH_RENAMED
#define arrow_path_fixture arrow_path_fixture_renamed
#endif

#ifdef ARROWPATH_VOLATILE_PATH
#define PATH_QUALIFIER volatile
#else
#define PATH_QUALIFIER
#endif

static int arrow_path_fixture(
    Game *game, PATH_QUALIFIER int *path, int length)
{
    int arrow_room;
    int index;

    arrow_room = game->location[PLAYER];

    for (index = 0; index < length; ++index) {
        if (adjacent_room(arrow_room, path[index]))
            arrow_room = path[index];
        else
            arrow_room = cave[arrow_room][random_index(TUNNELS)];

        if (arrow_room == game->location[WUMPUS]) {
            printf("AHA! YOU GOT THE WUMPUS!\n");
            flush_output();
            return WIN_GAME;
        }

        if (arrow_room == game->location[PLAYER]) {
            printf("OUCH! ARROW GOT YOU!\n");
            game->arrows = game->arrows - 1;
            flush_output();
            return LOSE_GAME;
        }
    }

    game->arrows = game->arrows - 1;
    printf("MISSED\n");
    flush_output();

    if (game->arrows <= 0) {
        printf("YOU RAN OUT OF ARROWS!\n");
        flush_output();
        return LOSE_GAME;
    }

#ifdef ARROWPATH_EXTRA_CFG
    if (game->cpu == 1234)
        return 7;
#endif
    return wake_game(game);
}

static int reference_adjacent(int from, int to)
{
    int index;

    for (index = TUNNELS - 1; index >= 0; --index)
        if (cave[from][index] == to)
            return 1;
    return 0;
}

static int reference_random_index(int limit)
{
    int value;

    value = random_values[random_cursor++];
    while (value < 0)
        value += limit;
    return limit == 0 ? 0 : value % limit;
}

static int reference_arrow(Game *game, int *path, int length)
{
    int room;
    int step;

    room = game->location[PLAYER];
    step = 0;
    while (step != length) {
        if (reference_adjacent(room, path[step]))
            room = path[step];
        else
            room = cave[room][reference_random_index(TUNNELS)];
        if (room == game->location[WUMPUS])
            return WIN_GAME;
        if (room == game->location[PLAYER]) {
            --game->arrows;
            return LOSE_GAME;
        }
        ++step;
    }
    --game->arrows;
    if (game->arrows <= 0)
        return LOSE_GAME;
    ++wake_calls;
    ++game->cpu;
    return wake_result;
}

static int run_case(
    int player, int wumpus, int arrows, int *path, int length,
    int random_value, int expected_random_count, int wake_value)
{
    Game actual;
    Game expected;
    int actual_result;
    int expected_result;
    int actual_wakes;
    int expected_wakes;
    int failures;
    int index;

    for (index = 0; index < LOCATIONS; ++index) {
        actual.location[index] = 10 + index;
        actual.initial[index] = 0;
    }
    actual.location[PLAYER] = player;
    actual.location[WUMPUS] = wumpus;
    actual.arrows = arrows;
    actual.cpu = 0;
    expected = actual;
    random_values[0] = random_value;
    random_count = expected_random_count;
    random_cursor = 0;
    wake_result = wake_value;
    wake_calls = 0;
    actual_result = arrow_path_fixture(&actual, path, length);
    actual_wakes = wake_calls;
    failures = random_cursor != random_count;

    random_cursor = 0;
    wake_calls = 0;
    expected_result = reference_arrow(&expected, path, length);
    expected_wakes = wake_calls;
    failures += random_cursor != random_count;
    failures += actual_result != expected_result;
    failures += actual.arrows != expected.arrows;
    failures += actual.cpu != expected.cpu;
    failures += actual_wakes != expected_wakes;
    return failures;
}

int main(void)
{
    int direct_hit[1] = {2};
    int self_hit[2] = {2, 1};
    int random_hit[1] = {20};
    int failures;

    failures = 0;
    failures += run_case(1, 2, 5, direct_hit, 1, 0, 0, 9);
    failures += run_case(1, 20, 5, self_hit, 2, 0, 0, 9);
    failures += run_case(1, 5, 5, random_hit, 1, 1, 1, 9);
    printf("arrow path failures=%d\n", failures);
    return failures != 0;
}
