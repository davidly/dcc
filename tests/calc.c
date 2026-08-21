/**
 * @file calc.c
 * @brief Expression-calculator front end used as an end-to-end DCC regression.
 *
 * @par Scenario
 * The test runner supplies deterministic command-line expressions that cover
 * signed 1024-bit integers, decimal arithmetic, division, modulus, powers,
 * bitwise operations, and equality. With no arguments, the same evaluator is
 * an interactive CP/M console calculator.
 *
 * @par Role
 * This module owns tokenization, recursive-descent parsing, mixed integer and
 * decimal dispatch, result formatting, commands, and BDOS console interaction.
 *
 * @par Build variants
 * The portable build leaves Z80_ASM_OPTS undefined:
 * @code{.sh}
 * ./dccmake tests/calc.c tests/calc1024.c tests/calcdoub.c \
 *   dcc-output=CALC
 * @endcode
 * The comparison build enables the optional hand-written integer kernels for
 * every translation unit and uses calcopt.c as the front-end wrapper:
 * @code{.sh}
 * ./dccmake tests/calcopt.c tests/calc1024.c tests/calcdoub.c \
 *   dcc-define=Z80_ASM_OPTS dcc-output=CALCOPT
 * @endcode
 * dcc-peep=true or false controls the separate peephole pass for either build;
 * it does not select between portable C and the Z80 kernels.
 *
 * @par Boundary
 * calc1024 owns integer representation and arithmetic; calcdoub owns decimal
 * representation and arithmetic. calcopt.c may include this file to build the
 * identical front end with optional Z80 arithmetic kernels enabled.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calcdoub.h"

#define MAX_LINE 640
#define MAX_NUMBER 320

extern int bdos(int function, int argument);

struct Value
{
    int is_double;
    struct CalcInt1024 int_value;
    struct CalcDouble double_value;
};

struct Parser
{
    char *text;
    int error;
    char error_message[40];
};

static void parse_expression(struct Parser *parser, struct Value *result);
static void parse_unary(struct Parser *parser, struct Value *result);

static int is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static int is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char *skip_spaces(char *text)
{
    while (*text != '\0' && is_space(*text))
        ++text;
    return text;
}

static int parse_minimum_integer(char **text, struct CalcInt1024 *value)
{
    const char *minimum;
    char *current;

    minimum = "89884656743115795386465259539451236680898848947115"
              "32863671504057886633790275048156635423866120376801"
              "05600569399356966788293948844072083112464237153197"
              "37062188883946712432742638151109800623047059726541"
              "47604250288441907534117123144073695655527041361858"
              "16752553422931491199736229692398581524176781648121"
              "12068608";
    current = skip_spaces(*text);
    while (*minimum != '\0' && *current == *minimum)
    {
        ++current;
        ++minimum;
    }
    if (*minimum != '\0' || is_digit(*current) || *current == '.' ||
        *current == 'e' || *current == 'E')
        return 0;
    xzero(value);
    value->word[CALC_INT_WORDS - 1] = 0x80000000UL;
    *text = current;
    return 1;
}

static int same_command(char *text, const char *command)
{
    text = skip_spaces(text);
    while (*text != '\0' && *command != '\0')
    {
        if (*text != *command)
        {
            return 0;
        }
        ++text;
        ++command;
    }
    if (*command != '\0')
    {
        return 0;
    }
    text = skip_spaces(text);
    return *text == '\0';
}

static void set_int(struct Value *value, int int_value)
{
    value->is_double = 0;
    xfrom(&value->int_value, int_value);
    dzero(&value->double_value);
}

static void set_integer(struct Value *value, const struct CalcInt1024 *int_value)
{
    value->is_double = 0;
    value->int_value = *int_value;
    dzero(&value->double_value);
}

static void set_double(struct Value *value,
                       const struct CalcDouble *double_value)
{
    value->is_double = 1;
    xzero(&value->int_value);
    value->double_value = *double_value;
}

static void value_as_double(const struct Value *value,
                            struct CalcDouble *double_value)
{
    if (value->is_double)
        *double_value = value->double_value;
    else
        dfromi(&value->int_value, double_value);
}

static void parser_error(struct Parser *parser, const char *message)
{
    if (parser->error == 0)
    {
        strncpy(parser->error_message, message, sizeof(parser->error_message) - 1);
        parser->error_message[sizeof(parser->error_message) - 1] = '\0';
    }
    parser->error = 1;
}

static void token_add(struct Parser *parser, char *token, int *index, char ch)
{
    if (*index >= MAX_NUMBER - 1)
    {
        parser_error(parser, "Number too long");
        return;
    }
    token[*index] = ch;
    *index = *index + 1;
}

static void parse_number(struct Parser *parser, struct Value *result)
{
    char token[MAX_NUMBER];
    int index;
    int saw_digit;
    int is_double;

    parser->text = skip_spaces(parser->text);
    index = 0;
    saw_digit = 0;
    is_double = 0;

    while (is_digit(*parser->text) && parser->error == 0)
    {
        saw_digit = 1;
        token_add(parser, token, &index, *parser->text);
        ++parser->text;
    }

    if (*parser->text == '.' && parser->error == 0)
    {
        is_double = 1;
        token_add(parser, token, &index, *parser->text);
        ++parser->text;
        while (is_digit(*parser->text) && parser->error == 0)
        {
            saw_digit = 1;
            token_add(parser, token, &index, *parser->text);
            ++parser->text;
        }
    }

    if ((*parser->text == 'e' || *parser->text == 'E') && parser->error == 0)
    {
        is_double = 1;
        token_add(parser, token, &index, *parser->text);
        ++parser->text;
        if (*parser->text == '+' || *parser->text == '-')
        {
            token_add(parser, token, &index, *parser->text);
            ++parser->text;
        }
        while (is_digit(*parser->text) && parser->error == 0)
        {
            saw_digit = 1;
            token_add(parser, token, &index, *parser->text);
            ++parser->text;
        }
    }

    if (parser->error != 0)
    {
        set_int(result, 0);
        return;
    }
    if (saw_digit == 0)
    {
        parser_error(parser, "Expected number");
        set_int(result, 0);
        return;
    }

    token[index] = '\0';
    if (is_double)
    {
        if (dparse(token, &result->double_value))
        {
            parser_error(parser, "Double out of range");
            set_int(result, 0);
            return;
        }
        result->is_double = 1;
        xzero(&result->int_value);
    }
    else
    {
        if (xparse(token, &result->int_value))
        {
            parser_error(parser, "Integer out of range");
            set_int(result, 0);
            return;
        }
        set_integer(result, &result->int_value);
    }
}

static void parse_primary(struct Parser *parser, struct Value *result)
{
    parser->text = skip_spaces(parser->text);
    if (*parser->text == '(')
    {
        ++parser->text;
        parse_expression(parser, result);
        parser->text = skip_spaces(parser->text);
        if (*parser->text != ')' && parser->error == 0)
        {
            parser_error(parser, "Expected )");
            set_int(result, 0);
            return;
        }
        if (*parser->text == ')')
        {
            ++parser->text;
        }
        return;
    }

    parse_number(parser, result);
}

static void parse_power(struct Parser *parser, struct Value *result)
{
    struct Value exponent;
    struct CalcDouble base_double;
    struct CalcDouble exponent_double;
    struct CalcDouble power_double;
    struct CalcInt1024 product;
    struct CalcInt1024 factor;
    struct CalcInt1024 next_product;
    struct CalcInt1024 zero;
    struct CalcInt1024 one;
    struct CalcInt1024 minus_one;
    int count;
    int exponent_count;
    int power_status;

    parse_primary(parser, result);
    parser->text = skip_spaces(parser->text);
    if (*parser->text != '^' || parser->error != 0)
    {
        return;
    }

    ++parser->text;
    parse_unary(parser, &exponent);
    if (parser->error != 0)
    {
        return;
    }
    xfrom(&zero, 0);
    xfrom(&one, 1);
    xfrom(&minus_one, -1);
    if (!result->is_double && !exponent.is_double &&
        !xisneg(&exponent.int_value))
    {
        count = 1;
        while (count < CALC_INT_WORDS &&
               exponent.int_value.word[count] == 0UL)
            ++count;
        if (count < CALC_INT_WORDS ||
            exponent.int_value.word[0] > 32767UL)
        {
            if (xcomp(&result->int_value, &zero) == 0 ||
                xcomp(&result->int_value, &one) == 0)
                return;
            if (xcomp(&result->int_value, &minus_one) == 0)
            {
                if ((exponent.int_value.word[0] & 1UL) == 0UL)
                    set_integer(result, &one);
                return;
            }
            parser_error(parser, "Integer overflow");
            set_int(result, 0);
            return;
        }
        exponent_count = (int)exponent.int_value.word[0];
        product = one;
        factor = result->int_value;
        while (exponent_count > 0)
        {
            if ((exponent_count & 1) != 0)
            {
                if (xmul(&product, &factor, &next_product))
                {
                    parser_error(parser, "Integer overflow");
                    set_int(result, 0);
                    return;
                }
                product = next_product;
            }
            exponent_count >>= 1;
            if (exponent_count != 0)
            {
                if (xmul(&factor, &factor, &next_product))
                {
                    parser_error(parser, "Integer overflow");
                    set_int(result, 0);
                    return;
                }
                factor = next_product;
            }
        }
        set_integer(result, &product);
        return;
    }
    value_as_double(result, &base_double);
    value_as_double(&exponent, &exponent_double);
    power_status = dpow(&base_double, &exponent_double, &power_double);
    if (power_status != 0)
    {
        if (power_status == 2)
            parser_error(parser, "Division by zero");
        else if (power_status == 3)
            parser_error(parser, "Double exponent must be an integer");
        else
            parser_error(parser, "Double out of range");
        set_int(result, 0);
        return;
    }
    set_double(result, &power_double);
}

static void parse_unary(struct Parser *parser, struct Value *result)
{
    parser->text = skip_spaces(parser->text);
    if (*parser->text == '+')
    {
        ++parser->text;
        parse_unary(parser, result);
        return;
    }
    if (*parser->text == '-')
    {
        ++parser->text;
        if (parse_minimum_integer(&parser->text, &result->int_value))
        {
            result->is_double = 0;
            dzero(&result->double_value);
            return;
        }
        parse_unary(parser, result);
        if (result->is_double)
        {
            struct CalcDouble negated_double;

            dneg(&result->double_value, &negated_double);
            set_double(result, &negated_double);
        }
        else
        {
            struct CalcInt1024 negated;

            if (xnegate(&result->int_value, &negated))
            {
                parser_error(parser, "Integer overflow");
                set_int(result, 0);
            }
            else
            {
                set_integer(result, &negated);
            }
        }
        return;
    }
    if (*parser->text == '~')
    {
        ++parser->text;
        parse_unary(parser, result);
        if (result->is_double)
        {
            parser_error(parser, "~ is for integers only");
            set_int(result, 0);
        }
        else
        {
            struct CalcInt1024 inverted;

            xnot(&result->int_value, &inverted);
            set_integer(result, &inverted);
        }
        return;
    }

    parse_power(parser, result);
}

static void parse_multiply(struct Parser *parser, struct Value *result)
{
    struct Value right;
    struct CalcDouble left_double;
    struct CalcDouble right_double;
    struct CalcDouble double_result;
    struct CalcInt1024 integer_result;
    struct CalcInt1024 remainder;
    char op;
    int integer_divide;
    int divide_status;
    int double_status;

    parse_unary(parser, result);
    while (parser->error == 0)
    {
        parser->text = skip_spaces(parser->text);
        op = *parser->text;
        if (op != '*' && op != '/' && op != '%')
        {
            break;
        }
        ++parser->text;
        integer_divide = 0;
        if (op == '/' && *parser->text == '/')
        {
            integer_divide = 1;
            ++parser->text;
        }
        parse_unary(parser, &right);

        if (integer_divide)
        {
            if (result->is_double || right.is_double)
            {
                parser_error(parser, "// is for integers only");
                set_int(result, 0);
                return;
            }
            if (xiszero(&right.int_value))
            {
                parser_error(parser, "Division by zero");
                set_int(result, 0);
                return;
            }
            divide_status = xdiv(&result->int_value, &right.int_value,
                                 &integer_result, &remainder);
            if (divide_status == 2)
            {
                parser_error(parser, "Integer overflow");
                set_int(result, 0);
                return;
            }
            set_integer(result, &integer_result);
        }
        else if (op == '%')
        {
            if (result->is_double || right.is_double)
            {
                parser_error(parser, "% is for integers only");
                set_int(result, 0);
                return;
            }
            if (xiszero(&right.int_value))
            {
                parser_error(parser, "Division by zero");
                set_int(result, 0);
                return;
            }
            divide_status = xdiv(&result->int_value, &right.int_value,
                                 &integer_result, &remainder);
            if (divide_status == 2)
            {
                parser_error(parser, "Integer overflow");
                set_int(result, 0);
                return;
            }
            set_integer(result, &remainder);
        }
        else if (result->is_double || right.is_double)
        {
            value_as_double(result, &left_double);
            value_as_double(&right, &right_double);
            if (op == '*')
                double_status = dmul(&left_double, &right_double,
                                     &double_result);
            else
                double_status = ddiv(&left_double, &right_double,
                                     &double_result);
            if (double_status != 0)
            {
                if (double_status == 2)
                    parser_error(parser, "Division by zero");
                else
                    parser_error(parser, "Double out of range");
                set_int(result, 0);
                return;
            }
            set_double(result, &double_result);
        }
        else if (op == '*')
        {
            if (xmul(&result->int_value, &right.int_value, &integer_result))
            {
                parser_error(parser, "Integer overflow");
                set_int(result, 0);
                return;
            }
            set_integer(result, &integer_result);
        }
        else
        {
            if (xiszero(&right.int_value))
            {
                parser_error(parser, "Division by zero");
                set_int(result, 0);
                return;
            }
            divide_status = xdiv(&result->int_value, &right.int_value,
                                 &integer_result, &remainder);
            if (divide_status == 2)
            {
                parser_error(parser, "Integer overflow");
                set_int(result, 0);
                return;
            }
            set_integer(result, &integer_result);
        }
    }
}

static void parse_add(struct Parser *parser, struct Value *result)
{
    struct Value right;
    struct CalcDouble left_double;
    struct CalcDouble right_double;
    struct CalcDouble double_result;
    struct CalcInt1024 integer_result;
    char op;
    int double_status;

    parse_multiply(parser, result);
    while (parser->error == 0)
    {
        parser->text = skip_spaces(parser->text);
        op = *parser->text;
        if (op != '+' && op != '-')
        {
            break;
        }
        ++parser->text;
        parse_multiply(parser, &right);

        if (result->is_double || right.is_double)
        {
            value_as_double(result, &left_double);
            value_as_double(&right, &right_double);
            if (op == '+')
                double_status = dadd(&left_double, &right_double,
                                     &double_result);
            else
                double_status = dsub(&left_double, &right_double,
                                     &double_result);
            if (double_status != 0)
            {
                parser_error(parser, "Double out of range");
                set_int(result, 0);
                return;
            }
            set_double(result, &double_result);
        }
        else if (op == '+')
        {
            if (xadd(&result->int_value, &right.int_value, &integer_result))
            {
                parser_error(parser, "Integer overflow");
                set_int(result, 0);
                return;
            }
            set_integer(result, &integer_result);
        }
        else
        {
            if (xsub(&result->int_value, &right.int_value, &integer_result))
            {
                parser_error(parser, "Integer overflow");
                set_int(result, 0);
                return;
            }
            set_integer(result, &integer_result);
        }
    }
}

static void parse_expression(struct Parser *parser, struct Value *result)
{
    struct Value right;
    struct CalcDouble left_double;
    struct CalcDouble right_double;

    parse_add(parser, result);
    while (parser->error == 0)
    {
        parser->text = skip_spaces(parser->text);
        if (*parser->text != '=')
        {
            break;
        }
        ++parser->text;
        parse_add(parser, &right);
        if (result->is_double || right.is_double)
        {
            value_as_double(result, &left_double);
            value_as_double(&right, &right_double);
            set_int(result, dcomp(&left_double, &right_double) == 0);
        }
        else
        {
            set_int(result, xcomp(&result->int_value, &right.int_value) == 0);
        }
    }
}

static int evaluate_expression(char *line, char *outbuf, int outsize)
{
    struct Parser parser;
    struct Value result;

    parser.text = line;
    parser.error = 0;
    parser.error_message[0] = '\0';
    parse_expression(&parser, &result);
    parser.text = skip_spaces(parser.text);
    if (parser.error == 0 && *parser.text != '\0')
    {
        parser_error(&parser, "Unexpected input");
    }
    if (parser.error != 0)
    {
        if (outbuf != NULL && outsize > 0)
        {
            strncpy(outbuf, parser.error_message, outsize - 1);
            outbuf[outsize - 1] = '\0';
        }
        return 1;
    }

    if (outbuf != NULL && outsize > 0)
    {
        if (result.is_double)
        {
            dfmt(&result.double_value, outbuf);
        }
        else
        {
            xfmt(&result.int_value, outbuf);
        }
    }
    return 0;
}

static void append_argument(char *line, int line_size, const char *text)
{
    int length;

    length = 0;
    while (line[length] != '\0')
    {
        ++length;
    }
    while (*text != '\0' && length < line_size - 1)
    {
        line[length] = *text;
        ++length;
        ++text;
    }
    line[length] = '\0';
}

static int read_line(char *line, int line_size)
{
    int character;
    int length;

    length = 0;
    line[0] = '\0';
    while (1)
    {
        do
        {
            character = bdos(6, 0xff) & 0xff;
        } while (character == 0);

        if (character == '\r' || character == '\n')
        {
            putchar('\r');
            putchar('\n');
            return 1;
        }
        if (character == 3)
        {
            putchar('\r');
            putchar('\n');
            return 0;
        }
        if (character == '\b' || character == 127)
        {
            if (length > 0)
            {
                --length;
                line[length] = '\0';
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        }
        else if (character >= 32 && character < 127 && length < line_size - 1)
        {
            line[length] = (char)character;
            ++length;
            line[length] = '\0';
            putchar(character);
        }
        fflush(stdout);
    }
}

static void print_help(void)
{
    printf("Available commands:\n");
    printf("  /help   Show this help text\n");
    printf("  /bye    Exit the calculator\n");
    printf("\n");
    printf("Integers are signed 1024-bit values.\n");
    printf("\n");
    printf("Operators:\n");
    printf("  +   add\n");
    printf("  -   subtract or unary negation\n");
    printf("  +   unary plus\n");
    printf("  *   multiply\n");
    printf("  /   divide\n");
    printf("  //  integer division (integers only)\n");
    printf("  %%   modulus (integers only)\n");
    printf("  ^   power (exponent)\n");
    printf("  ~   unary bitwise not (integers only)\n");
    printf("  =   equality comparison\n");
    printf("  parentheses: ( and )\n");
    printf("\n");
    printf("Examples:\n");
    printf("  2 + 2          -> 4\n");
    printf("  10.0 / 4       -> 2.5\n");
    printf("  10 // 4        -> 2\n");
    printf("  (3 + 5) * 2    -> 16\n");
    printf("  7 %% 3          -> 1\n");
    printf("  2 ^ 3          -> 8\n");
    printf("  ~5             -> -6\n");
    printf("  -5 + 3         -> -2\n");
}

int main(int argc, char *argv[])
{
    char line[MAX_LINE];
    char result[MAX_LINE];
    int index;

    if (argc > 1)
    {
        line[0] = '\0';
        for (index = 1; index < argc; ++index)
        {
            if (index > 1)
            {
                append_argument(line, sizeof(line), " ");
            }
            append_argument(line, sizeof(line), argv[index]);
        }
        if (same_command(line, "/help"))
        {
            print_help();
            return 0;
        }
        if (evaluate_expression(line, result, sizeof(result)) != 0)
        {
            printf("Error: %s\n", result);
            return 1;
        }
        printf("Result: %s\n", result);
        return 0;
    }

    printf("Type /help for help\n");
    while (1)
    {
        printf("Calc> ");
        fflush(stdout);
        if (!read_line(line, sizeof(line)))
        {
            return 0;
        }
        if (same_command(line, "/bye"))
        {
            return 0;
        }
        if (same_command(line, "/help"))
        {
            print_help();
            continue;
        }
        if (*skip_spaces(line) != '\0')
        {
            if (evaluate_expression(line, result, sizeof(result)) != 0)
            {
                printf("Error: %s\n", result);
            }
            else
            {
                printf("Result: %s\n", result);
            }
        }
    }
}
