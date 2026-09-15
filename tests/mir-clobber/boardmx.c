#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

#define BOARD_SIZE 8

#ifdef BOARDMX_VOLATILE_BOARD
volatile bool board_matrix[BOARD_SIZE][BOARD_SIZE];
#elif defined(BOARDMX_CHAR_BOARD)
char board_matrix[BOARD_SIZE][BOARD_SIZE];
#elif defined(BOARDMX_SHORT_ROW)
bool board_matrix[BOARD_SIZE][7];
#else
bool board_matrix[BOARD_SIZE][BOARD_SIZE];
#endif

#ifdef BOARDMX_RENAMED
#define board_matrix_print board_matrix_print_renamed
#endif

#if defined(BOARDMX_UNSIGNED_SIZE)
typedef unsigned int board_matrix_size_t;
#elif defined(BOARDMX_CHAR_SIZE)
typedef signed char board_matrix_size_t;
#else
typedef int board_matrix_size_t;
#endif

#ifdef BOARDMX_WRAPPED_PRINTF
static int board_matrix_printf(const char *format, ...)
{
    int result;
    va_list arguments;

    va_start(arguments, format);
    result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}
#define BOARDMX_VALUE_PRINT board_matrix_printf
#define BOARDMX_NEWLINE_PRINT board_matrix_printf
#elif defined(BOARDMX_SPLIT_PRINT)
static int board_matrix_value_print(const char *format, int value)
{
    return printf(format, value);
}

static int board_matrix_newline_print(const char *format)
{
    return printf(format);
}
#define BOARDMX_VALUE_PRINT board_matrix_value_print
#define BOARDMX_NEWLINE_PRINT board_matrix_newline_print
#else
#define BOARDMX_VALUE_PRINT printf
#define BOARDMX_NEWLINE_PRINT printf
#endif

#ifdef BOARDMX_NONVOID
int board_matrix_print(const board_matrix_size_t size)
#else
void board_matrix_print(const board_matrix_size_t size)
#endif
{
#ifdef BOARDMX_CFG_GUARD
    if (size < 0)
        return;
#endif
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column)
            BOARDMX_VALUE_PRINT("%2d ", board_matrix[row][column]);
        BOARDMX_NEWLINE_PRINT("\n");
    }
    BOARDMX_NEWLINE_PRINT("\n");
#ifdef BOARDMX_NONVOID
    return size;
#endif
}

int main(void)
{
    int failures;

    board_matrix[0][0] = true;
    board_matrix[0][2] = true;
    board_matrix[1][1] = true;
    board_matrix[2][0] = true;
    board_matrix[2][1] = true;

    board_matrix_print(3);
    failures = board_matrix[0][0] != true;
    failures += board_matrix[0][1] != false;
    failures += board_matrix[0][2] != true;
    failures += board_matrix[1][0] != false;
    failures += board_matrix[1][1] != true;
    failures += board_matrix[1][2] != false;
    failures += board_matrix[2][0] != true;
    failures += board_matrix[2][1] != true;
    failures += board_matrix[2][2] != false;
    printf("board matrix failures=%d\n", failures);
    return failures != 0;
}
