/**
 * @file dcc_data.c
 * @brief Emits string, initialized-data, BSS, and stack-layout assembly.
 *
 * @par Role
 * Serializes string literals and global initializer records, marks required
 * externs, lays out uninitialized objects for application or module mode, and
 * publishes data, BSS, heap, and stack boundary symbols.
 *
 * @par Key entry points
 * emit_data(), emit_init_numeric(), and emit_init_label_or_number().
 *
 * @par Boundary
 * dcc_global_init.c builds initializer records and dcc_symbols.c owns the
 * referenced symbols. This module emits data layout, not function bodies.
 */

#include "dcc.h"
int init_label_is_number(const char *p)
{
    if (*p == '-' || *p == '+')
        p++;

    if (!isdigit((unsigned char)*p))
        return 0;

    while (isdigit((unsigned char)*p))
        p++;

    return *p == 0;
}

void emit_init_numeric(long v, int bytes)
{
    unsigned long uv;

    /* Treat initializer storage as raw object bits.  This matters for
     * float constants such as -3.0f whose IEEE representation has the
     * high bit set.  On MSVC, long is 32-bit and atol()/signed shifts can
     * corrupt values above LONG_MAX, so keep emission unsigned here. */
    uv = (unsigned long)v;

    if (bytes == 1) {
        fprintf(g_emit_sink.stream, "\tdb %lu\n", uv & 255UL);
    } else if (bytes == 4) {
        fprintf(g_emit_sink.stream, "\tdw %lu\n", uv & 0xffffUL);
        fprintf(g_emit_sink.stream, "\tdw %lu\n", (uv >> 16) & 0xffffUL);
    } else {
        fprintf(g_emit_sink.stream, "\tdw %lu\n", uv & 0xffffUL);
    }
}

int init_label_is_string_literal_label(const char *p)
{
    if (*p != 'S')
        return 0;

    p++;
    if (!isdigit((unsigned char)*p))
        return 0;

    while (isdigit((unsigned char)*p))
        p++;

    return *p == 0;
}

static void mark_init_label_extrn(const char *p)
{
    char base[64];
    int i;
    int n;
    struct Sym *s;

    if (init_label_is_number(p) || init_label_is_string_literal_label(p))
        return;

    for (n = 0; p[n] && p[n] != '+' && p[n] != '-' && n < (int)sizeof(base) - 1; ++n)
        base[n] = p[n];
    base[n] = 0;

    s = find_global(base);
    if (s == NULL) {
        for (i = 0; i < nglobals; ++i) {
            if (!strcmp(asm_name_for(sym_asm_name(&globals[i])), base)) {
                s = &globals[i];
                break;
            }
        }
    }
    emit_extrn_if_needed(s);
}

static void emit_init_zero_bytes(int bytes);

void emit_init_label_or_number(const char *p, int bytes)
{
    long v;

    if (init_label_is_number(p)) {
        v = (long)strtoul(p, NULL, 10);
        emit_init_numeric(v, bytes);
        return;
    }

    /*
     * Symbolic initializers are addresses.  String literal labels are
     * compiler-generated assembly labels (S0, S1, ...).  Labels that contain
     * '+' or '-' are already-mangled asm arithmetic expressions of the form
     * "_sym±offset" (produced for pointer±constant initialisers); emit them
     * verbatim so M80 can compute the relocatable value.  Ordinary C symbol
     * names still need the normal target name mapping.
     */
    mark_init_label_extrn(p);
    if (init_label_is_string_literal_label(p)) {
        fprintf(g_emit_sink.stream, "\tdw %s\n", p);
    } else if (strchr(p, '+') || strchr(p, '-')) {
        fprintf(g_emit_sink.stream, "\tdw %s\n", p);
    } else {
        fprintf(g_emit_sink.stream, "\tdw %s\n", asm_name_for(p));
    }

    if (bytes > 2)
        emit_init_zero_bytes(bytes - 2);
}

static void emit_init_zero_bytes(int bytes)
{
    while (bytes >= 2) {
        fprintf(g_emit_sink.stream, "\tdw 0\n");
        bytes -= 2;
    }
    if (bytes > 0)
        fprintf(g_emit_sink.stream, "\tdb 0\n");
}

void emit_data(void)
{
    int i, j;
    struct Sym *s;

    flush_pending_asm();
    emit("\n\t; string literals\n");

    for (i = 0; i < nstrings; ++i) {
        int col;
        char buf[8];
        int vlen;

        fprintf(g_emit_sink.stream, "S%d:\n", i);

        if (string_wide[i]) {
            emit("\tdw ");
            col = 4; /* tab + "dw " */

            for (j = 0; j < string_len[i]; ++j) {
                sprintf(buf, "%u", (unsigned char)strings[i][j]);
                vlen = (int)strlen(buf);
                if (j) {
                    if (col + 1 + vlen > 96) {
                        emit("\n\tdw ");
                        col = 4;
                    } else {
                        emit(",");
                        col += 1;
                    }
                }
                emit(buf);
                col += vlen;
            }

            if (j) {
                if (col + 2 > 96) {
                    emit("\n\tdw 0\n");
                } else {
                    emit(",0\n");
                }
            } else {
                emit("0\n");
            }
        } else {
            emit("\tdb ");
            col = 4; /* tab + "db " */

            for (j = 0; j < string_len[i]; ++j) {
                sprintf(buf, "%u", (unsigned char)strings[i][j]);
                vlen = (int)strlen(buf);
                if (j) {
                    /* Break before emitting comma+value if it would push past 96 chars,
                       leaving comfortable room for the trailing ",0" */
                    if (col + 1 + vlen > 96) {
                        emit("\n\tdb ");
                        col = 4;
                    } else {
                        emit(",");
                        col += 1;
                    }
                }
                emit(buf);
                col += vlen;
            }

            /* Terminating null byte */
            if (j) {
                if (col + 2 > 96) {
                    emit("\n\tdb 0\n");
                } else {
                    emit(",0\n");
                }
            } else {
                emit("0\n");
            }
        }
    }

    emit("\n\t; initialized globals\n");

    for (i = 0; i < nglobals; ++i) {
        s = &globals[i];

        if (s->storage == SC_FUNC || s->storage == SC_EXTERN) continue;
        /* skip BSS (uninitialized) globals — emitted separately below */
        if (!(s->has_init && s->init_count > 0) && !(s->has_init && !s->is_array)) continue;

        if (!s->is_static) {
            asm_name_check_public_collision(sym_asm_name(s));
            fprintf(g_emit_sink.stream, "\tpublic %s\n", asm_name_for(sym_asm_name(s)));
        }
        emit_debug_global(s);
        fprintf(g_emit_sink.stream, "%s:\n", asm_name_for(sym_asm_name(s)));
        if (s->has_init && s->init_count > 0) {
            int j;
            int elem_bytes;
            int used_bytes;

            elem_bytes = s->is_array ? s->elem_size : type_size(s->type);
            if (elem_bytes <= 0)
                elem_bytes = type_size(s->type);
            if (elem_bytes <= 0)
                elem_bytes = 2;

            used_bytes = 0;
            for (j = 0; j < s->init_count; ++j) {
                 {
                    int ib;
                    ib = s->init_sizes[j] ? s->init_sizes[j] : elem_bytes;
                    emit_init_label_or_number(s->init_labels[j], ib);
                    used_bytes += ib;
                }
            }

            if (s->size > used_bytes)
                emit_init_zero_bytes(s->size - used_bytes);
        } else {
            emit_init_numeric(s->init_value, type_size(s->type));
        }
    }

    /* BSS: uninitialized globals.
     *
     * Do not emit DS bytes for BSS.  With M80/L80, DS in the normal
     * relocatable stream is still reflected in the .COM image size.
     * COMMON also changes placement and can put BSS before/over code in
     * simple .COM links.
     *
     * Instead, mark the physical end of emitted code/data and make each
     * uninitialized global an EQU alias at an offset from __bssb.  This
     * gives every BSS object a stable run-time address immediately after
     * the loaded image without adding bytes to the .COM file.
     */
    {
        int bss_off;
        int bss_size;

        if (opt_module) {
            /* A separately linked helper module cannot share the final app's
             * synthetic __bssb EQU space: doing so would overlap BSS from
             * multiple modules and also create duplicate boundary publics.
             * Use ordinary DS storage here.  This may increase COM size for
             * helper modules with writable globals, but it is link-safe. */
            emit("\n\t; module uninitialized globals\n");
            for (i = 0; i < nglobals; ++i) {
                s = &globals[i];

                if (s->storage == SC_FUNC || s->storage == SC_EXTERN) continue;
                if ((s->has_init && s->init_count > 0) || (s->has_init && !s->is_array)) continue;

                bss_size = s->size > 0 ? s->size : 2;
                if (!s->is_static) {
                    asm_name_check_public_collision(sym_asm_name(s));
                    fprintf(g_emit_sink.stream, "\tpublic %s\n", asm_name_for(sym_asm_name(s)));
                }
                emit_debug_global(s);
                fprintf(g_emit_sink.stream, "%s:\n", asm_name_for(sym_asm_name(s)));
                fprintf(g_emit_sink.stream, "\tds %d\n", bss_size);
            }
            return;
        }

        bss_off = 0;
        {
            int effective_stack_size;
            int min_stack_size;

            effective_stack_size = opt_stack_size;
            min_stack_size = max_function_local_bytes + 128;
            if (min_stack_size < 0)
                min_stack_size = max_function_local_bytes;
            if (effective_stack_size < min_stack_size)
                effective_stack_size = min_stack_size;

            emit("\n\tpublic\t__stack_size\n");
            fprintf(g_emit_sink.stream, "__stack_size\tequ\t%d\n", effective_stack_size);
            if (effective_stack_size != opt_stack_size)
                fprintf(g_emit_sink.stream, "\t; dcc: raised stack reserve from %d to %d; max local frame is %d bytes\n",
                        opt_stack_size, effective_stack_size, max_function_local_bytes);
        }
        emit("\n\tpublic\t__data_end\n__data_end:\n");
        emit("\tpublic\t__bssb\n__bssb:\n");

        for (i = 0; i < nglobals; ++i) {
            s = &globals[i];

            if (s->storage == SC_FUNC || s->storage == SC_EXTERN) continue;
            /* skip initialized globals — already emitted above */
            if ((s->has_init && s->init_count > 0) || (s->has_init && !s->is_array)) continue;

            bss_size = s->size > 0 ? s->size : 2;
            emit_debug_global(s);
            fprintf(g_emit_sink.stream, "%s\tequ\t__bssb+%d\n", asm_name_for(sym_asm_name(s)), bss_off);
            bss_off += bss_size;
        }

        fprintf(g_emit_sink.stream, "__bsse\tequ\t__bssb+%d\n", bss_off);
        fprintf(g_emit_sink.stream, "__hstart\tequ\t__bsse\n");
        emit("\tpublic\t__bsse\n");
        emit("\tpublic\t__hstart\n");
    }
}

