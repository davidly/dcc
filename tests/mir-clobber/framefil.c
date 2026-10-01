/* Recursive-frame-fill structural controls; unbounded forms require the guard. */
#include <stdio.h>

#ifdef FF_RENAMED
#define fill_frame renamed_frame
#define frame_sink renamed_sink
#define values renamed_values
#define index renamed_index
#endif

#ifdef FF_LARGE
#define FRAME_COUNT 128
#define FRAME_MASK 127
#else
#define FRAME_COUNT 8
#define FRAME_MASK 7
#endif

#ifdef FF_VOLATILE
#define FRAME_QUALIFIER volatile
#else
#define FRAME_QUALIFIER
#endif

int frame_sink;

#ifdef FF_KNR
int fill_frame(depth)
int depth;
#else
int fill_frame(int depth)
#endif
{
    FRAME_QUALIFIER int values[FRAME_COUNT];
#ifdef FF_SIGNED
    char index;
#elif defined(FF_UNSIGNED)
    unsigned char index;
#else
    int index;
#endif
#ifdef FF_BOUNDED
    if (depth >= 5)
        return 7;
#endif
    for (index = 0; index < FRAME_COUNT; ++index)
#ifdef FF_SUBTRACT
        values[index] = depth - index;
#else
        values[index] = depth + index;
#endif
#ifdef FF_SHIFTED_SINK
    frame_sink = values[(depth + 1) & FRAME_MASK];
#else
    frame_sink = values[depth & FRAME_MASK];
#endif
#ifdef FF_RETURN_NEXT
    return fill_frame(depth + 1) + values[1];
#else
    return fill_frame(depth + 1) + values[0];
#endif
}

int main(void)
{
#ifdef FF_BOUNDED
    int result;
    int expected_result = 17;
    int expected_sink = 8;
#ifdef FF_RETURN_NEXT
    expected_result = 21;
#endif
#ifdef FF_SUBTRACT
    expected_sink = 0;
#endif
#ifdef FF_SHIFTED_SINK
    expected_sink = 9;
#endif
    result = fill_frame(1);
    printf("frame fill result=%d sink=%d\n", result, frame_sink);
    if (result != expected_result || frame_sink != expected_sink)
        return 1;
    puts("frame fill failures=0");
#else
    puts("frame fill guarded start");
    frame_sink = fill_frame(1);
    puts("frame fill unexpected return");
#endif
    return 0;
}
