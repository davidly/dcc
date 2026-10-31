/* Native typed-bank generation. Input is compiler-resolved metadata, never C text. */
#define BANK_MAX_ARGS 16
#define BANK_MAX_FUNCS 256
#define BANK_MAX_RECORDS 128
#define BANK_MAX_FIELDS 1024

struct BankFunction {
    char name[64], result[80], args[BANK_MAX_ARGS][80];
    char parameters[BANK_MAX_ARGS][64];
    int nargs, variadic, definition;
    int direction[BANK_MAX_ARGS], count[BANK_MAX_ARGS], bound[BANK_MAX_ARGS];
    char signature[256];
    int frame_max;
};

struct BankField {
    char name[64], type[24];
    int offset, count;
};

struct BankRecord {
    char name[64];
    int size, count, flat, first, used;
};

struct BankApi {
    struct BankFunction functions[BANK_MAX_FUNCS];
    struct BankRecord records[BANK_MAX_RECORDS];
    struct BankField fields[BANK_MAX_FIELDS];
    int count, record_count, field_count;
};

static int bank_error(const char *name, const char *reason)
{
    fprintf(stderr, "typed bank API %s: %s\n", name, reason);
    return 0;
}

static int bank_ensure_dir(const char *path)
{
    char parent[MAX_PATH_LEN];
    const char *name;
    size_t length;
    if (dir_exists(path)) return 1;
    copy_text(parent, sizeof(parent), path);
    length = strlen(parent);
    while (length > 1 && (parent[length - 1] == '/' || parent[length - 1] == '\\'))
        parent[--length] = 0;
    name = base_name(parent);
    if (name > parent) {
        parent[name - parent] = 0;
        if (!bank_ensure_dir(parent)) return 0;
    }
    return ensure_dir(path);
}

static int bank_width(const char *type)
{
    if (!strcmp(type, "void")) return 0;
    if (!strcmp(type, "i8") || !strcmp(type, "u8")) return 1;
    if (!strcmp(type, "i16") || !strcmp(type, "u16")) return 2;
    if (!strcmp(type, "i32") || !strcmp(type, "u32")) return 4;
    return -1;
}

static const char *bank_record_name(const char *type)
{
    if (!strncmp(type, "pr:", 3)) return type + 3;
    if (!strncmp(type, "pcr:", 4)) return type + 4;
    return NULL;
}

static int bank_record_find(const struct BankApi *api, const char *name)
{
    int i;
    for (i = 0; i < api->record_count; ++i)
        if (!strcmp(api->records[i].name, name)) return i;
    return -1;
}

static const char *bank_ctype(const char *type)
{
    static char record_type[96];
    const char *record = bank_record_name(type);
    if (record) {
        snprintf(record_type, sizeof(record_type), "%sstruct %s *",
                 type[1] == 'c' ? "const " : "", record);
        return record_type;
    }
    if (!strcmp(type, "void")) return "void";
    if (!strcmp(type, "i8")) return "signed char";
    if (!strcmp(type, "u8")) return "unsigned char";
    if (!strcmp(type, "i16")) return "int";
    if (!strcmp(type, "u16")) return "unsigned int";
    if (!strcmp(type, "i32")) return "long";
    if (!strcmp(type, "u32")) return "unsigned long";
    if (!strcmp(type, "pi8")) return "char *";
    if (!strcmp(type, "pci8")) return "const char *";
    if (!strcmp(type, "pcu8")) return "const unsigned char *";
    return "unsigned char *";
}

static int bank_find(struct BankApi *api, const char *name)
{
    int i;
    for (i = 0; i < api->count; ++i)
        if (!strcmp(api->functions[i].name, name)) return i;
    return -1;
}

static int bank_same_type(const struct BankFunction *a, const struct BankFunction *b)
{
    int i;
    if (strcmp(a->result, b->result) || a->nargs != b->nargs ||
        a->variadic != b->variadic) return 0;
    for (i = 0; i < a->nargs; ++i)
        if (strcmp(a->args[i], b->args[i])) return 0;
    return 1;
}

static int bank_read_record(struct BankApi *api, FILE *f, const char *line)
{
    struct BankRecord record;
    char member[MAX_LINE_LEN], extra[2];
    int i, j, old, referenced = 0;
    memset(&record, 0, sizeof(record));
    if (sscanf(line, "R %63s %d %d %d %1s", record.name, &record.size,
               &record.count, &record.flat, extra) != 4 ||
        !library_symbol(record.name, strlen(record.name)) ||
        record.size < 0 || record.count < 0 || record.count > BANK_MAX_FIELDS ||
        (record.flat != 0 && record.flat != 1))
        return bank_error("record", "invalid compiler record metadata");
    for (i = 0; i < api->count; ++i)
        for (j = 0; j < api->functions[i].nargs; ++j) {
            const char *tag = bank_record_name(api->functions[i].args[j]);
            if (tag && !strcmp(tag, record.name)) referenced = 1;
        }
    if (!referenced) {
        for (i = 0; i < record.count; ++i)
            if (!fgets(member, sizeof(member), f) || member[0] != 'M')
                return bank_error(record.name, "missing compiler field metadata");
        return 1;
    }
    old = bank_record_find(api, record.name);
    if (old >= 0) {
        const struct BankRecord *previous = &api->records[old];
        if (record.size != previous->size || record.count != previous->count ||
            record.flat != previous->flat)
            return bank_error(record.name, "incompatible record layouts across modules");
        record.first = previous->first;
    } else {
        if (api->record_count == BANK_MAX_RECORDS ||
            record.count > BANK_MAX_FIELDS - api->field_count)
            return bank_error(record.name, "too much record metadata");
        record.first = api->field_count;
    }
    for (i = 0; i < record.count; ++i) {
        struct BankField field;
        memset(&field, 0, sizeof(field));
        if (!fgets(member, sizeof(member), f) ||
            sscanf(member, "M %63s %23s %d %d %1s", field.name, field.type,
                   &field.offset, &field.count, extra) != 4 ||
            !library_symbol(field.name, strlen(field.name)) ||
            field.offset < 0 || field.count < 0)
            return bank_error(record.name, "invalid compiler field metadata");
        if (old >= 0) {
            const struct BankField *previous = &api->fields[record.first + i];
            if (strcmp(field.name, previous->name) ||
                strcmp(field.type, previous->type) ||
                field.offset != previous->offset || field.count != previous->count)
                return bank_error(record.name, "incompatible record fields across modules");
        } else api->fields[api->field_count++] = field;
    }
    if (old < 0) api->records[api->record_count++] = record;
    return 1;
}

static int bank_read(struct BankApi *api, const char *path, int declarations)
{
    FILE *f = fopen(path, "r");
    char line[MAX_LINE_LEN];
    if (!f) return bank_error(path, "cannot read compiler metadata");
    if (!fgets(line, sizeof(line), f) || strcmp(line, "DCCBANK 2\n")) {
        fclose(f);
        return bank_error(path, "invalid compiler metadata version");
    }
    while (fgets(line, sizeof(line), f)) {
        struct BankFunction fn;
        int i, old, consumed = 0;
        char *p;
        memset(&fn, 0, sizeof(fn));
        if (line[0] == 'R') {
            if (!bank_read_record(api, f, line)) { fclose(f); return 0; }
            continue;
        }
        if (line[0] == 'N' && !declarations) {
            char name[64];
            if (sscanf(line, "N %63s %n", name, &consumed) != 1 ||
                (old = bank_find(api, name)) < 0) {
                fclose(f);
                return bank_error(path, "invalid parameter name metadata");
            }
            p = line + consumed;
            for (i = 0; i < api->functions[old].nargs; ++i) {
                char *name_out = api->functions[old].parameters[i];
                if (sscanf(p, "%63s %n", name_out, &consumed) != 1 ||
                    !library_symbol(name_out, strlen(name_out))) {
                    fclose(f);
                    return bank_error(path, "invalid parameter name metadata");
                }
                p += consumed;
            }
            continue;
        }
        if (line[0] != 'F') continue;
        if (sscanf(line, "F %63s %d %79s %d %d %n", fn.name,
                   &fn.definition, fn.result, &fn.nargs, &fn.variadic,
                   &consumed) != 5 || fn.nargs < 0 || fn.nargs > BANK_MAX_ARGS) {
            fclose(f);
            return bank_error(path, "invalid or excessive parameter metadata (max 16)");
        }
        p = line + consumed;
        for (i = 0; i < fn.nargs; ++i) {
            if (sscanf(p, "%79s %n", fn.args[i], &consumed) != 1) {
                fclose(f);
                return bank_error(fn.name, "missing parameter metadata");
            }
            p += consumed;
        }
        if (!declarations && !fn.definition) continue;
        old = bank_find(api, fn.name);
        if (old >= 0) {
            if (!bank_same_type(&api->functions[old], &fn) ||
                (!declarations && fn.definition)) {
                fclose(f);
                return bank_error(fn.name, "duplicate definition or incompatible declarations");
            }
            continue;
        }
        if (api->count == BANK_MAX_FUNCS) {
            fclose(f);
            return bank_error(path, "too many API declarations");
        }
        api->functions[api->count++] = fn;
    }
    fclose(f);
    return 1;
}

static int bank_annotations(struct BankApi *api, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[MAX_LINE_LEN];
    if (!f) return bank_error(path, "cannot read buffer annotations");
    while (fgets(line, sizeof(line), f)) {
        char name[64], direction[16], extra[2];
        unsigned int arg, count, bound;
        int n, index, dir;
        struct BankFunction *fn;
        int record_annotation = line[0] == 'T';
        if (line[0] != 'B' && !record_annotation) continue;
        if (record_annotation) {
            count = 0;
            bound = 0;
            n = sscanf(line, "T %63s %u %15s %1s", name, &arg, direction, extra);
        } else
            n = sscanf(line, "B %63s %u %15s %u %u %1s",
                       name, &arg, direction, &count, &bound, extra);
        if (n != (record_annotation ? 3 : 5) || !arg ||
            (!record_annotation && (!count || bound > 256 || !bound))) {
            fclose(f);
            return bank_error(path, record_annotation ?
                "expected #pragma dcc bank_record FUNCTION ARG in|out|inout (1-based index)" :
                "expected #pragma dcc bank_buffer FUNCTION ARG in|out|inout COUNT MAX (1-based indices; MAX 1..256)");
        }
        index = bank_find(api, name);
        if (index < 0) {
            fclose(f);
            return bank_error(name, "copy annotation has no public API declaration");
        }
        fn = &api->functions[index];
        dir = !strcmp(direction, "in") ? 1 : !strcmp(direction, "out") ? 2 :
              !strcmp(direction, "inout") ? 3 : 0;
        if (!dir || arg > (unsigned)fn->nargs) {
            fclose(f);
            return bank_error(name, record_annotation ?
                "invalid copy direction or argument index" :
                "buffer needs byte pointer and separate u16 count parameter");
        }
        if (record_annotation) {
            const char *tag = bank_record_name(fn->args[arg - 1]);
            int record = tag ? bank_record_find(api, tag) : -1;
            if (record < 0) {
                fclose(f);
                return bank_error(name, "bank_record requires a pointer to a named flat struct");
            }
            bound = api->records[record].size;
        } else if (count > (unsigned)fn->nargs ||
            (strcmp(fn->args[arg - 1], "pi8") && strcmp(fn->args[arg - 1], "pu8") &&
             strcmp(fn->args[arg - 1], "pci8") && strcmp(fn->args[arg - 1], "pcu8")) ||
            strcmp(fn->args[count - 1], "u16")) {
            fclose(f);
            return bank_error(name, "buffer needs byte pointer and separate u16 count parameter");
        }
        --arg;
        if (fn->direction[arg] && (fn->direction[arg] != dir ||
            fn->count[arg] != (int)count - 1 || fn->bound[arg] != (int)bound)) {
            fclose(f);
            return bank_error(name, "conflicting copy annotations");
        }
        fn->direction[arg] = dir;
        fn->count[arg] = (int)count - 1;
        fn->bound[arg] = (int)bound;
        if (fn->args[arg][1] == 'c' && dir != 1) {
            fclose(f);
            return bank_error(name, "const pointer permits only direction in");
        }
    }
    fclose(f);
    return 1;
}

static int bank_signature_append(struct BankFunction *fn, const char *part)
{
    if (strlen(fn->signature) + strlen(part) + 2 > sizeof(fn->signature))
        return bank_error(fn->name, "canonical signature exceeds 255 characters");
    strcat(fn->signature, part);
    return 1;
}

static int bank_record_signature(struct BankApi *api, struct BankFunction *fn, int arg)
{
    const char *tag = bank_record_name(fn->args[arg]);
    int index = bank_record_find(api, tag), i, offset = 0;
    struct BankRecord *record;
    char part[192];
    if (index < 0)
        return bank_error(fn->name, "missing resolved record layout");
    record = &api->records[index];
    if (!record->flat)
        return bank_error(tag, "record must be a complete flat struct of non-const, non-volatile integers or fixed one-dimensional integer arrays; unions, bitfields, nested structures and pointer members are unsupported");
    snprintf(part, sizeof(part), "rec1le:%s[%d]{", tag, record->size);
    if (!bank_signature_append(fn, part)) return 0;
    for (i = 0; i < record->count; ++i) {
        const struct BankField *field = &api->fields[record->first + i];
        int width = bank_width(field->type), elements = field->count ? field->count : 1;
        if (width <= 0 || elements > 256 / width || field->offset != offset)
            return bank_error(tag, "invalid or oversized packed integer field layout");
        offset += width * elements;
        if (field->count)
            snprintf(part, sizeof(part), "%s%s:%s[%d]@%d", i ? ";" : "",
                     field->name, field->type, field->count, field->offset);
        else
            snprintf(part, sizeof(part), "%s%s:%s@%d", i ? ";" : "",
                     field->name, field->type, field->offset);
        if (!bank_signature_append(fn, part)) return 0;
    }
    if (offset != record->size)
        return bank_error(tag, "record size differs from its packed fields");
    record->used = 1;
    return bank_signature_append(fn, "}");
}

static int bank_signature(struct BankApi *api, struct BankFunction *fn)
{
    int i, worst = bank_width(fn->result);
    if (fn->variadic)
        return bank_error(fn->name, "varargs and unprototyped/K&R functions are unsupported");
    if (bank_width(fn->result) < 0)
        return bank_error(fn->name, "unsupported result (pointers, function pointers, floats and structures are not transferable)");
    snprintf(fn->signature, sizeof(fn->signature), "%s(", fn->result);
    for (i = 0; i < fn->nargs; ++i) {
        char part[80];
        int width = bank_width(fn->args[i]);
        if (fn->direction[i]) {
            const char *dir = fn->direction[i] == 1 ? "in" :
                              fn->direction[i] == 2 ? "out" : "inout";
            if (fn->count[i] < 0) {
                snprintf(part, sizeof(part), "%s%s:", i ? "," : "", dir);
                if (!bank_signature_append(fn, part) ||
                    !bank_record_signature(api, fn, i)) return 0;
                worst += fn->bound[i];
                continue;
            }
            snprintf(part, sizeof(part), "%s%s:u8[%d<=%d]", i ? "," : "",
                     dir, fn->count[i] + 1, fn->bound[i]);
            worst += fn->bound[i];
        } else {
            if (width <= 0)
                return bank_error(fn->name, "unsupported argument: every byte pointer needs a bank_buffer annotation and every named struct pointer needs a bank_record annotation; pointer graphs, function pointers, floats and by-value structures are unsupported");
            snprintf(part, sizeof(part), "%s%s", i ? "," : "", fn->args[i]);
            worst += width;
        }
        if (!bank_signature_append(fn, part)) return 0;
    }
    strcat(fn->signature, ")");
    if (worst > 256)
        return bank_error(fn->name, "maximum packed frame exceeds runtime's 256-byte cap");
    fn->frame_max = worst ? worst : 1;
    return 1;
}

static void bank_parameters(FILE *out, const struct BankFunction *fn)
{
    int i;
    if (!fn->nargs) fprintf(out, "void");
    for (i = 0; i < fn->nargs; ++i)
        fprintf(out, "%s%s a%d", i ? ", " : "", bank_ctype(fn->args[i]), i);
}

static void bank_zero_return(FILE *out, const struct BankFunction *fn)
{
    fprintf(out, !strcmp(fn->result, "void") ? "return;" : "return 0;");
}

static int bank_scalar_size(const struct BankFunction *fn)
{
    int i, size = bank_width(fn->result);
    for (i = 0; i < fn->nargs; ++i)
        if (!fn->direction[i]) size += bank_width(fn->args[i]);
    return size;
}

static int bank_fixed_size(const struct BankFunction *fn)
{
    int i, size = bank_scalar_size(fn);
    for (i = 0; i < fn->nargs; ++i)
        if (fn->direction[i] && fn->count[i] < 0) size += fn->bound[i];
    return size;
}

static void bank_copy_length(char *text, size_t cap, const struct BankFunction *fn, int arg)
{
    if (fn->count[arg] < 0) snprintf(text, cap, "%d", fn->bound[arg]);
    else snprintf(text, cap, "a%d", fn->count[arg]);
}

static void bank_emit(FILE *host, FILE *adapter, const struct BankFunction *fn, int index)
{
    int i, j, offset, result = bank_width(fn->result), fixed = bank_fixed_size(fn);
    int scalar = bank_scalar_size(fn), dynamic = 0;
    char name[64], body[24];
    for (i = 0; i < fn->nargs; ++i)
        if (fn->direction[i] && fn->count[i] >= 0) dynamic = 1;
    if (dynamic) copy_text(body, sizeof(body), "n");
    else snprintf(body, sizeof(body), "%d", fixed);
    upper_copy(name, sizeof(name), fn->name);
    fprintf(adapter, "\nchar b1s%02d[] = \"%s\";\n", index, fn->signature);
    fprintf(adapter, "unsigned int b1e%02d(unsigned char *wire)\n{\n unsigned char *f;\n", index);
    fprintf(host, "\n%s %s(", bank_ctype(fn->result), fn->name);
    bank_parameters(host, fn);
    fprintf(host, ")\n{\n static bank_library_binding_t binding;\n unsigned char f[%d];\n", fn->frame_max);
    if (dynamic) {
        fprintf(host, " unsigned int n = %d, p;\n", fixed);
        fprintf(adapter, " unsigned int n = %d, p;\n", fixed);
    }
    if (result) {
        fprintf(host, " %s r = 0;\n", bank_ctype(fn->result));
        fprintf(adapter, " %s r;\n", bank_ctype(fn->result));
    }
    for (i = 0; i < fn->nargs; ++i)
        fprintf(adapter, " %s a%d = 0;\n", bank_ctype(fn->args[i]), i);
    if (dynamic)
        fprintf(adapter, " if (!wire || b1get(wire) < %d || b1get(wire) > 256) return 0;\n f = wire + 2;\n", fixed);
    else
        fprintf(adapter, " if (!wire || b1get(wire) != %d) return 0;\n f = wire + 2;\n", fixed);
    offset = result;
    for (i = 0; i < fn->nargs; ++i) {
        if (!fn->direction[i]) {
            int width = bank_width(fn->args[i]);
            fprintf(adapter, " memcpy(&a%d, f + %d, %d);\n", i, offset, width);
            offset += width;
        }
    }
    for (i = 0; i < fn->nargs; ++i) {
        if (fn->direction[i]) {
            int c = fn->count[i];
            if (c < 0) {
                fprintf(host, " if (!a%d) { blfail(BL_ARGUMENT); ", i);
                bank_zero_return(host, fn);
                fprintf(host, " }\n");
                continue;
            }
            fprintf(host, " if (a%d > %d || a%d > 256 - n) { blfail(BL_BOUNDS); ",
                    c, fn->bound[i], c);
            bank_zero_return(host, fn);
            fprintf(host, " }\n if (a%d && !a%d) { blfail(BL_ARGUMENT); ", c, i);
            bank_zero_return(host, fn);
            fprintf(host, " }\n n += a%d;\n", c);
            fprintf(adapter, " if (a%d > %d || a%d > 256 - n) return 0;\n n += a%d;\n",
                    c, fn->bound[i], c, c);
        }
    }
    for (i = 0; i < fn->nargs; ++i) {
        if (!fn->direction[i]) continue;
        for (j = i + 1; j < fn->nargs; ++j) {
            char left[24], right[24];
            if (!fn->direction[j] || (fn->count[i] >= 0 && fn->count[j] >= 0) ||
                !((fn->direction[i] | fn->direction[j]) & 2)) continue;
            bank_copy_length(left, sizeof(left), fn, i);
            bank_copy_length(right, sizeof(right), fn, j);
            fprintf(host, " if (%s && %s && ((size_t)a%d <= (size_t)a%d ? "
                    "(size_t)a%d - (size_t)a%d < %s : "
                    "(size_t)a%d - (size_t)a%d < %s)) { blfail(BL_ARGUMENT); ",
                    left, right, i, j, j, i, left, i, j, right);
            bank_zero_return(host, fn);
            fprintf(host, " }\n");
        }
    }
    if (dynamic)
        fprintf(adapter, " if (b1get(wire) != n) return 0;\n p = %d;\n", scalar);
    if (result) fprintf(host, " memset(f, 0, %d);\n", result);
    offset = result;
    for (i = 0; i < fn->nargs; ++i) {
        if (!fn->direction[i]) {
            int width = bank_width(fn->args[i]);
            fprintf(host, " memcpy(f + %d, &a%d, %d);\n", offset, i, width);
            offset += width;
        }
    }
    if (dynamic) fprintf(host, " p = %d;\n", scalar);
    offset = scalar;
    for (i = 0; i < fn->nargs; ++i) {
        if (fn->direction[i]) {
            char length[24], position[24];
            bank_copy_length(length, sizeof(length), fn, i);
            if (dynamic) copy_text(position, sizeof(position), "p");
            else snprintf(position, sizeof(position), "%d", offset);
            fprintf(adapter, " a%d = (%s)(f + %s);\n",
                    i, bank_ctype(fn->args[i]), position);
            if (fn->count[i] >= 0) fprintf(host, " if (%s)", length);
            if (fn->direction[i] & 1)
                fprintf(host, " memcpy(f + %s, a%d, %s);\n", position, i, length);
            else
                fprintf(host, " memset(f + %s, 0, %s);\n", position, length);
            if (dynamic) {
                fprintf(adapter, " p += %s;\n", length);
                fprintf(host, " p += %s;\n", length);
            } else offset += fn->bound[i];
        }
    }
    fprintf(adapter, " %s%s(", result ? "r = " : "", fn->name);
    for (i = 0; i < fn->nargs; ++i) fprintf(adapter, "%sa%d", i ? ", " : "", i);
    fprintf(adapter, ");\n");
    if (result) fprintf(adapter, " memcpy(f, &r, %d);\n", result);
    fprintf(adapter, " return 1;\n}\n");
    fprintf(host, " if (!bank_library_call_bound(&binding, \"%s\", \"%s\", f, %s)) { ",
            name, fn->signature, body);
    bank_zero_return(host, fn);
    fprintf(host, " }\n");
    if (dynamic) fprintf(host, " p = %d;\n", scalar);
    offset = scalar;
    for (i = 0; i < fn->nargs; ++i) {
        if (fn->direction[i]) {
            char length[24], position[24];
            bank_copy_length(length, sizeof(length), fn, i);
            if (dynamic) copy_text(position, sizeof(position), "p");
            else snprintf(position, sizeof(position), "%d", offset);
            if (fn->direction[i] & 2) {
                if (fn->count[i] >= 0) fprintf(host, " if (%s)", length);
                fprintf(host, " memcpy(a%d, f + %s, %s);\n", i, position, length);
            }
            if (dynamic) fprintf(host, " p += %s;\n", length);
            else offset += fn->bound[i];
        }
    }
    if (result)
        fprintf(host, " memcpy(&r, f, %d);\n return r;\n}\n", result);
    else
        fprintf(host, " return;\n}\n");
}

static int bank_records_match(const struct BankApi *a, const struct BankApi *b,
                              const struct BankFunction *fn)
{
    int i, j;
    for (i = 0; i < fn->nargs; ++i) {
        const char *tag = bank_record_name(fn->args[i]);
        const struct BankRecord *left, *right;
        int ai, bi;
        if (!tag) continue;
        ai = bank_record_find(a, tag);
        bi = bank_record_find(b, tag);
        if (ai < 0 || bi < 0)
            return bank_error(tag, "record missing from authoritative API header");
        left = &a->records[ai];
        right = &b->records[bi];
        if (left->size != right->size || left->count != right->count ||
            left->flat != right->flat)
            return bank_error(tag, "record layout differs from authoritative API header");
        for (j = 0; j < left->count; ++j) {
            const struct BankField *x = &a->fields[left->first + j];
            const struct BankField *y = &b->fields[right->first + j];
            if (strcmp(x->name, y->name) || strcmp(x->type, y->type) ||
                x->offset != y->offset || x->count != y->count)
                return bank_error(tag, "record fields differ from authoritative API header");
        }
    }
    return 1;
}

static void bank_emit_records(FILE *out, const struct BankApi *api, int definitions)
{
    int i, j;
    fprintf(out, "#include <stddef.h>\n");
    for (i = 0; i < api->record_count; ++i) {
        const struct BankRecord *record = &api->records[i];
        if (!record->used) continue;
        if (definitions) {
            fprintf(out, "\nstruct %s {\n", record->name);
            for (j = 0; j < record->count; ++j) {
                const struct BankField *field = &api->fields[record->first + j];
                fprintf(out, " %s %s", bank_ctype(field->type), field->name);
                if (field->count) fprintf(out, "[%d]", field->count);
                fprintf(out, ";\n");
            }
            fprintf(out, "};\n");
        }
        fprintf(out, "#if !defined(__INTELLISENSE__) /* Editor uses host layout, not the Z80 ABI. */\n");
        fprintf(out, "_Static_assert(sizeof(struct %s) == %d, \"bank record size: %s\");\n",
                record->name, record->size, record->name);
        for (j = 0; j < record->count; ++j) {
            const struct BankField *field = &api->fields[record->first + j];
            int width = bank_width(field->type);
            fprintf(out, "_Static_assert(offsetof(struct %s, %s) == %d && "
                    "sizeof(%s) == %d, \"bank field layout: %s.%s\");\n",
                    record->name, field->name, field->offset,
                    bank_ctype(field->type), width, record->name, field->name);
        }
        fprintf(out, "#endif\n");
    }
}

static int bank_generate(struct Config *cfg)
{
    struct BankApi *api = calloc(1, sizeof(*api)), *defs = calloc(1, sizeof(*defs));
    char metadata[MAX_PATH_LEN], assembly[MAX_PATH_LEN], cmd[MAX_CMD_LEN];
    char adapter_path[MAX_PATH_LEN], host_path[MAX_PATH_LEN], api_path[MAX_PATH_LEN];
    char cwd[MAX_PATH_LEN];
    FILE *adapter = NULL, *host = NULL, *header = NULL;
    int i, j, ok = 0, header_written = 0;
    int automatic = str_ieq(cfg->library_api, "auto");
    if (!api || !defs) goto done;
    if (str_ieq(cfg->output, "B1ADAPT")) {
        bank_error(cfg->output, "reserved generated module name");
        goto done;
    }
    path_join(adapter_path, sizeof(adapter_path), cfg->build_dir, "B1ADAPT.C");
    path_join(host_path, sizeof(host_path), cfg->library_host_dir, "B1HOST.C");
    remove(adapter_path);
    remove(host_path);
    if (!cfg->contract_only) {
        char output[MAX_NAME_LEN], artifact[MAX_PATH_LEN], filename[MAX_PATH_LEN];
        upper_copy(output, sizeof(output), cfg->output);
        snprintf(filename, sizeof(filename), "%s.LIB", output);
        path_join(artifact, sizeof(artifact), cfg->build_dir, filename);
        remove(artifact);
        lower_copy(output, sizeof(output), cfg->output);
        snprintf(filename, sizeof(filename), "%s.lib", output);
        path_join(artifact, sizeof(artifact), cfg->build_dir, filename);
        remove(artifact);
    }
    if (!GETCWD(cwd, sizeof(cwd))) goto done;
    if (automatic) {
        char relative[MAX_PATH_LEN];
        path_join(relative, sizeof(relative), cfg->library_host_dir, "API.H");
        if (relative[0] == '/' || relative[0] == '\\' ||
            (isalpha((unsigned char)relative[0]) && relative[1] == ':'))
            copy_text(api_path, sizeof(api_path), relative);
        else path_join(api_path, sizeof(api_path), cwd, relative);
        if (file_exists(api_path)) {
            char marker[128];
            FILE *existing = fopen(api_path, "r");
            int owned = existing && fgets(marker, sizeof(marker), existing) &&
                        !strncmp(marker, "/* Generated by dccmake;", 23);
            if (existing) fclose(existing);
            if (!owned) {
                bank_error(api_path, "refusing to overwrite a handwritten API header");
                goto done;
            }
            if (remove(api_path)) {
                bank_error(api_path, "cannot invalidate old generated API header");
                goto done;
            }
        }
    } else if (cfg->library_api[0] == '/' || cfg->library_api[0] == '\\' ||
        (isalpha((unsigned char)cfg->library_api[0]) && cfg->library_api[1] == ':'))
        copy_text(api_path, sizeof(api_path), cfg->library_api);
    else path_join(api_path, sizeof(api_path), cwd, cfg->library_api);
    if (strpbrk(api_path, "\"\r\n")) {
        bank_error(cfg->library_api, "API path cannot contain quotes or newlines");
        goto done;
    }
    for (i = automatic ? 0 : -1; i < cfg->input_count; ++i) {
        char filename[32];
        snprintf(filename, sizeof(filename), "B1M%02d.API", i + 1);
        path_join(metadata, sizeof(metadata), cfg->build_dir, filename);
        snprintf(filename, sizeof(filename), "B1M%02d.MAC", i + 1);
        path_join(assembly, sizeof(assembly), cfg->build_dir, filename);
        if (!build_dcc_command(cfg, i < 0 ? 1 : i,
                               i < 0 ? cfg->library_api : cfg->inputs[i],
                               assembly, cmd, sizeof(cmd)) ||
            !cmd_arg(cmd, sizeof(cmd), "-bank-api") ||
            !cmd_arg(cmd, sizeof(cmd), metadata) || !run_cmd(cmd))
            goto done;
        if (!bank_read(i < 0 ? api : defs, metadata, i < 0)) goto done;
        if (i < 0 && !bank_annotations(api, metadata)) goto done;
    }
    if (!defs->count || defs->count > 8) {
        bank_error(cfg->library_api, "auto discovery requires 1..8 public C definitions (none may be silently dropped)");
        goto done;
    }
    if (automatic) {
        for (i = 0; i < cfg->input_count; ++i) {
            char filename[32];
            snprintf(filename, sizeof(filename), "B1M%02d.API", i + 1);
            path_join(metadata, sizeof(metadata), cfg->build_dir, filename);
            if (!bank_annotations(defs, metadata)) goto done;
        }
    }
    for (i = 0; i < defs->count; ++i) {
        struct BankFunction *fn = &defs->functions[i];
        char upper[64];
        if (!automatic) {
            j = bank_find(api, fn->name);
            if (j < 0) { bank_error(fn->name, "public definition missing from authoritative API header"); goto done; }
            if (!bank_same_type(fn, &api->functions[j])) {
                bank_error(fn->name, "definition differs from authoritative API header"); goto done;
            }
            if (!bank_records_match(defs, api, fn)) goto done;
            memcpy(fn->direction, api->functions[j].direction, sizeof(fn->direction));
            memcpy(fn->count, api->functions[j].count, sizeof(fn->count));
            memcpy(fn->bound, api->functions[j].bound, sizeof(fn->bound));
        }
        if (strlen(fn->name) > 8) {
            bank_error(fn->name, "public export name exceeds 8 characters"); goto done;
        }
        upper_copy(upper, sizeof(upper), fn->name);
        if (!strncmp(upper, "B1E0", 4) || !strncmp(upper, "B1S0", 4)) {
            bank_error(fn->name, "name collides with reserved generated adapter/signature symbols");
            goto done;
        }
        for (j = 0; j < i; ++j) {
            char previous[64];
            upper_copy(previous, sizeof(previous), defs->functions[j].name);
            if (!strcmp(upper, previous)) {
                bank_error(fn->name, "case-folded export name collision"); goto done;
            }
            if (!strncmp(upper, previous, 5)) {
                bank_error(fn->name, "M80 six-significant-character external symbol collision");
                goto done;
            }
        }
        if (!bank_signature(defs, fn)) goto done;
        snprintf(cfg->exports[i], sizeof(cfg->exports[i]), "%s=_b1e%02d", upper, i);
    }
    if (cfg->input_count == MAX_ITEMS) { bank_error("auto", "too many input modules"); goto done; }
    for (i = 0; i < cfg->input_count; ++i) {
        char base[MAX_NAME_LEN];
        base_no_ext(base, sizeof(base), cfg->inputs[i]);
        if (str_ieq(base, "B1ADAPT") || (str_ieq(base, cfg->output) && i > 0)) {
            bank_error(base, "reserved or duplicate generated module name"); goto done;
        }
    }
    if (!bank_ensure_dir(cfg->library_host_dir)) {
        bank_error(cfg->library_host_dir, "cannot create generated host directory"); goto done;
    }
    if (automatic) {
        header = fopen(api_path, "w");
        if (!header) { bank_error(api_path, "cannot write generated API header"); goto done; }
        header_written = 1;
        fprintf(header, "/* Generated by dccmake; library source is authoritative. */\n"
                "#pragma once\n\n");
        bank_emit_records(header, defs, 1);
        for (i = 0; i < defs->count; ++i) {
            const struct BankFunction *fn = &defs->functions[i];
            for (j = 0; j < fn->nargs; ++j) {
                if (fn->direction[j] && fn->count[j] < 0)
                    fprintf(header, "#pragma dcc bank_record %s %d %s\n",
                            fn->name, j + 1, fn->direction[j] == 1 ? "in" :
                            fn->direction[j] == 2 ? "out" : "inout");
                else if (fn->direction[j])
                    fprintf(header, "#pragma dcc bank_buffer %s %d %s %d %d\n",
                            fn->name, j + 1, fn->direction[j] == 1 ? "in" :
                            fn->direction[j] == 2 ? "out" : "inout",
                            fn->count[j] + 1, fn->bound[j]);
            }
            fprintf(header, "%s %s(", bank_ctype(fn->result), fn->name);
            if (!fn->nargs) fprintf(header, "void");
            for (j = 0; j < fn->nargs; ++j) {
                fprintf(header, "%s%s ", j ? ", " : "", bank_ctype(fn->args[j]));
                if (fn->parameters[j][0]) fprintf(header, "%s", fn->parameters[j]);
                else fprintf(header, "arg%d", j + 1);
            }
            fprintf(header, ");\n");
        }
        if (ferror(header)) {
            bank_error(api_path, "cannot write complete generated API header"); goto done;
        }
        if (fclose(header)) {
            header = NULL;
            remove(api_path);
            bank_error(api_path, "cannot close generated API header"); goto done;
        }
        header = NULL;
    }
    adapter = fopen(adapter_path, "w");
    host = fopen(host_path, "w");
    if (!adapter || !host) { bank_error("auto", "cannot write generated source"); goto done; }
    fprintf(adapter, "/* Generated by dccmake; do not edit. */\n#include \"%s\"\n#include <string.h>\n", api_path);
    fprintf(host, "/* Generated by dccmake; do not edit. */\n#include \"%s\"\n#include \"BANKLIB.H\"\n#include <string.h>\n", automatic ? "API.H" : api_path);
    if (!automatic) {
        bank_emit_records(adapter, defs, 0);
        bank_emit_records(host, defs, 0);
    }
    fprintf(adapter, "static unsigned int b1get(unsigned char *p) { return p[0] | ((unsigned int)p[1] << 8); }\n");
    for (i = 0; i < defs->count; ++i) {
        bank_emit(host, adapter, &defs->functions[i], i);
        printf("dccmake: typed export %s %s\n", cfg->exports[i], defs->functions[i].signature);
    }
    if (ferror(adapter) || ferror(host)) goto done;
    copy_text(cfg->inputs[cfg->input_count++], MAX_PATH_LEN, adapter_path);
    cfg->export_count = defs->count;
    ok = 1;
done:
    if (header) { fclose(header); remove(api_path); }
    if (adapter && fclose(adapter)) ok = 0;
    if (host && fclose(host)) ok = 0;
    if (!ok && header_written) remove(api_path);
    if (!ok && adapter) remove(adapter_path);
    if (!ok && host) remove(host_path);
    if (!ok) bank_error("auto", "contract generation failed");
    free(api);
    free(defs);
    return ok;
}
