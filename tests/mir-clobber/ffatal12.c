#include <stdio.h>
#include <stdlib.h>

struct Ff12Stmt {
    int label;
    char *text;
    int unit;
    int op;
    struct Ff12Stmt *target;
    int target_label;
    int fmt;
    int sym;
    char *a;
    char *b;
    char *c;
    int ae;
    int be;
    int ce;
    int act;
    struct Ff12Stmt *act_target;
    int act_target_label;
    int act_sym;
    char *act_idx;
    char *act_rhs;
    int act_idx_e;
    int act_rhs_e;
    int ntargets;
    struct Ff12Stmt *targets[10];
};

static struct Ff12Stmt *ff12_statements;
#ifdef FF12_UNSIGNED_COUNT
static unsigned int ff12_statement_count;
#else
static int ff12_statement_count;
#endif
static struct Ff12Stmt *ff12_program_counter;

#ifdef FF12_FIXED_PRINT
static int ff12_print(
    FILE *stream, const char *format, const char *message,
    int index, const char *text)
{
    return fprintf(stream, format, message, index, text);
}
#endif

#ifdef FF12_FAST_EXIT
_Noreturn void ff12_exit_body(int status)
{
    exit(status);
}

extern _Noreturn void __fastcall ff12_exit(int status);
#asm
_ff12_exit:
        push    hl
        call    _ff12_exit_body
        pop     bc
        ret
#endasm
#endif

static _Noreturn void ff12_die(const char *message)
{
#ifdef FF12_FIXED_PRINT
    ff12_print(
#else
    fprintf(
#endif
            stderr,
#ifdef FF12_ALT_FORMAT
            "fortran-alt:%s index=%d text='%s'\n",
#else
            "forint:%s near pc=%d '%s'\n",
#endif
            message,
            (ff12_statements && ff12_program_counter)
                ? (int)(ff12_program_counter - ff12_statements) : -1,
            (ff12_statements &&
             ff12_program_counter >= ff12_statements &&
             ff12_program_counter <
                 ff12_statements + ff12_statement_count)
                ? ff12_program_counter->text : "");
#ifdef FF12_FAST_EXIT
    ff12_exit(1);
#else
    exit(1);
#endif
}

int main(void)
{
    static struct Ff12Stmt statements[2];

    statements[0].text = "ZERO";
    statements[1].text = "LINE";
    ff12_statements = statements;
    ff12_statement_count = 2;
    ff12_program_counter = statements + 1;
    printf("FF12 oracle index=1 text=LINE stride=%u\n",
           (unsigned int)sizeof(struct Ff12Stmt));
    ff12_die("boom");
}
