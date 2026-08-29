#ifndef TCMD_H
#define TCMD_H

struct CmdState {
    int value;
    int commands;
    int errors;
};

typedef int (*CmdAction)(struct CmdState *state, int argument);

struct CmdEntry {
    const char *name;
    CmdAction action;
    int needs_argument;
};

extern struct CmdEntry cmdtab_command_table[];
extern int cmdcnt_command_count;

int cmdset_set_value(struct CmdState *state, int argument);
int cmdadd_add_value(struct CmdState *state, int argument);
int cmdmul_multiply_value(struct CmdState *state, int argument);
int cmdneg_negate_value(struct CmdState *state, int argument);
int cmdnum_parse_number(const char **text, int *value);
int cmddis_dispatch(struct CmdState *state, const char *name, int argument,
                    int has_argument);
int cmdrun_run_script(struct CmdState *state, const char *script);

#endif
