/* dccpeep_internal.h - private line-program contract for dccpeep modules. */
#ifndef DCCPEEP_INTERNAL_H
#define DCCPEEP_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#define MAX_LINES 400000
#define MAX_LINE  512

extern char *lines[MAX_LINES];
extern char *user_asm_original[MAX_LINES];
extern int nlines;
extern int input_is_dcc_generated;

char *xstrdup2(const char *s);
int eq(int i, const char *s);
int starts_label(const char *s);
int is_blank_or_comment(const char *s);
void strip_peep_comment_copy(char *dst, const char *src);
void strip_peep_comment_lower_copy(char *dst, const char *src);
void replace1(int i, const char *s);
void replace1_tagged(int i, const char *s, const char *tag);
void delete_n(int i, int count);
void insert_line(int i, const char *s);
void insert_line_tagged(int i, const char *s, const char *tag);
void read_file(const char *name);
void write_file(const char *name);

#endif
