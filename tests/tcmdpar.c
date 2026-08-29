#include "tcmd.h"

static void skipsp(const char **text)
{
    while (**text == ' ' || **text == '\t' || **text == '\n')
        ++*text;
}

int cmdnum_parse_number(const char **text, int *value)
{
    int sign = 1;
    int result = 0;
    int digits = 0;

    skipsp(text);
    if (**text == '-') {
        sign = -1;
        ++*text;
    }
    while (**text >= '0' && **text <= '9') {
        result = result * 10 + (**text - '0');
        ++*text;
        digits++;
    }
    *value = result * sign;
    return digits != 0;
}

int cmdrun_run_script(struct CmdState *state, const char *script)
{
    char name[8];
    int length;
    int argument;
    int has_argument;

    while (1) {
        skipsp(&script);
        if (*script == '\0')
            return state->errors == 0;
        length = 0;
        while (*script >= 'a' && *script <= 'z') {
            if (length < (int)sizeof(name) - 1)
                name[length++] = *script;
            ++script;
        }
        name[length] = '\0';
        skipsp(&script);
        has_argument = cmdnum_parse_number(&script, &argument);
        if (!cmddis_dispatch(state, name, argument, has_argument))
            state->errors++;
        else
            state->commands++;
        skipsp(&script);
        if (*script == ';')
            ++script;
        else if (*script != '\0')
            state->errors++;
    }
}
