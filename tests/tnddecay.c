/* N-dimensional array partial-decay regression.
 *
 * A single subscript of a 3-D array still denotes a 2-D array.  In a value
 * context it must decay to the address of that plane, while sizeof must retain
 * the complete remaining array type.  Exercise global/local storage, byte,
 * word and long elements, parameter passing, pointer comparisons, pointer
 * arithmetic at the outer dimension, and a side-effecting subscript. */
#include <stdio.h>

static int words[2][3][4];
static unsigned char bytes[2][3][4];
static long longs[2][2][3];
static int plane_index;
static int failures;

static void check(const char *name, long got, long expected)
{
    if (got != expected) {
        printf("FAIL %s got %ld expected %ld\n", name, got, expected);
        ++failures;
    }
}

static int word_plane(int plane[3][4])
{
    return plane[0][0] + plane[2][3];
}

static int byte_plane(unsigned char plane[3][4])
{
    return (int)plane[0][0] + (int)plane[2][3];
}

static long long_plane(long plane[2][3])
{
    return plane[0][0] + plane[1][2];
}

static int local_plane(void)
{
    int local[2][3][4];

    local[1][0][0] = 31;
    local[1][2][3] = 37;
    return word_plane(local[1]);
}

int main(void)
{
    words[0][0][0] = 3;
    words[0][2][3] = 5;
    words[1][0][0] = 7;
    words[1][2][3] = 11;
    bytes[1][0][0] = 13;
    bytes[1][2][3] = 17;
    longs[1][0][0] = 100000L;
    longs[1][1][2] = 200000L;

    check("global word plane", word_plane(words[1]), 18L);
    check("local word plane", local_plane(), 68L);
    check("byte plane", byte_plane(bytes[1]), 30L);
    check("long plane", long_plane(longs[1]), 300000L);

    check("plane row address", words[1] == &words[1][0], 1L);
    check("outer pointer address", &words[1] == words + 1, 1L);
    check("plane sizeof", (long)sizeof words[1],
          3L * 4L * (long)sizeof(int));
    check("row sizeof", (long)sizeof words[1][2],
          4L * (long)sizeof(int));

    plane_index = 0;
    check("postfix plane value", word_plane(words[plane_index++]), 8L);
    check("postfix plane count", plane_index, 1L);

    if (failures) {
        printf("tnddecay failed: %d\n", failures);
        return 1;
    }
    printf("tnddecay completed with great success\n");
    return 0;
}
