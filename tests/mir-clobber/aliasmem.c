#include <stdio.h>
#include <string.h>

struct VolatileBytes {
    volatile unsigned char bytes[4];
};

struct PlainBytes {
    unsigned char bytes[4];
};

struct NestedBytes {
    struct VolatileBytes member;
};

typedef volatile unsigned char *VolatilePointer;
typedef VolatilePointer *VolatilePointerPointer;

struct PointerFields {
    volatile unsigned char *value, plain;
    volatile unsigned char **deep;
    unsigned char * volatile changing;
};

static volatile unsigned char **deep_global;

static int failures;

static void check(const char *name, int actual, int expected)
{
    if (actual != expected) {
        printf("FAIL %s: %d expected %d\n", name, actual, expected);
        ++failures;
    }
}

int nmember(struct PlainBytes *object, int index)
{
    return object->bytes[index + 1] + object->bytes[index + 1] +
           object->bytes[index + 1];
}

unsigned int nmword(struct PlainBytes *object)
{
    return (unsigned int)object->bytes[0] |
           ((unsigned int)object->bytes[1] << 8);
}

int vnested(struct NestedBytes *object, int index)
{
    return object->member.bytes[index + 1] + object->member.bytes[index + 1] +
           object->member.bytes[index + 1];
}

void vmstore(struct VolatileBytes *object, int index)
{
    object->bytes[index + 1] = 5;
    object->bytes[index + 1] = 7;
}

int vmember(struct VolatileBytes *object, int index)
{
    return object->bytes[index + 1] + object->bytes[index + 1] +
           object->bytes[index + 1];
}

unsigned int vmword(struct VolatileBytes *object)
{
    return (unsigned int)object->bytes[0] |
           ((unsigned int)object->bytes[1] << 8);
}

int vptrptr(volatile unsigned char **pointer, int index)
{
    volatile unsigned char *value = *pointer;
    return value[index + 1] + value[index + 1] + value[index + 1];
}

int vindirect(volatile unsigned char **pointer, int index)
{
    return (*pointer)[index + 1] + (*pointer)[index + 1] +
           (*pointer)[index + 1];
}

int vtypedef(VolatilePointerPointer pointer, int index)
{
    return (*pointer)[index + 1] + (*pointer)[index + 1] +
           (*pointer)[index + 1];
}

int vold(pointer, index)
volatile unsigned char **pointer;
int index;
{
    return (*pointer)[index + 1] + (*pointer)[index + 1] +
           (*pointer)[index + 1];
}

int vglobal(int index)
{
    return (*deep_global)[index + 1] + (*deep_global)[index + 1] +
           (*deep_global)[index + 1];
}

int vlocal(volatile unsigned char **pointer, int index)
{
    {
        volatile unsigned char **copy = pointer;
        return (*copy)[index + 1] + (*copy)[index + 1] +
               (*copy)[index + 1];
    }
}

int vpfield(struct PointerFields *object, int index)
{
    return object->value[index + 1] + object->value[index + 1] +
           object->value[index + 1];
}

int vdfield(struct PointerFields *object, int index)
{
    return (*object->deep)[index + 1] + (*object->deep)[index + 1] +
           (*object->deep)[index + 1];
}

int vchange(struct PointerFields *object, int index)
{
    return object->changing[index + 1] + object->changing[index + 1] +
           object->changing[index + 1];
}

int nindirect(unsigned char **pointer, int index)
{
    return (*pointer)[index + 1] + (*pointer)[index + 1] +
           (*pointer)[index + 1];
}

int vboth(volatile unsigned char * volatile *pointer, int index)
{
    return (*pointer)[index + 1] + (*pointer)[index + 1] +
           (*pointer)[index + 1];
}

int nlocal(unsigned char **pointer, int index)
{
    {
        unsigned char **copy = pointer;
        return (*copy)[index + 1];
    }
}

int vstatic(volatile unsigned char **pointer, int index)
{
    static volatile unsigned char **saved;
    saved = pointer;
    return (*saved)[index + 1] + (*saved)[index + 1] +
           (*saved)[index + 1];
}

int aliaswrite(unsigned char *value, unsigned char *alias, int index)
{
    int before = value[index + 1];
    *alias = 7;
    return before + value[index + 1] + value[index + 1];
}

void mutate(unsigned char *alias)
{
    *alias = 9;
}

int aliascall(unsigned char *value, unsigned char *alias, int index)
{
    int before = value[index + 1];
    mutate(alias);
    return before + value[index + 1] + value[index + 1];
}

int abranch(unsigned char *value, unsigned char *alias, int index, int flag)
{
    int before = value[index + 1];
    if (flag)
        *alias = 11;
    return before + value[index + 1] + value[index + 1];
}

int acopy(unsigned char *value, unsigned char *alias, int index)
{
    int before = value[index + 1];
    unsigned char replacement = 13;
    memcpy(alias, &replacement, 1);
    return before + value[index + 1] + value[index + 1];
}

int main(void)
{
    volatile unsigned char observed[3];
    volatile unsigned char *pointer = observed;
    volatile unsigned char * volatile changing_pointer = observed;
    unsigned char bytes[3];
    struct VolatileBytes object;
    struct PlainBytes ordinary;
    struct NestedBytes nested;
    struct PointerFields fields;
    unsigned char *ordinary_pointer = bytes;

    observed[1] = 3;
    bytes[1] = 5;
    object.bytes[0] = 0x12;
    object.bytes[1] = 0x34;
    ordinary.bytes[0] = 0x12;
    ordinary.bytes[1] = 0x34;
    nested.member.bytes[1] = 3;
    fields.value = observed;
    fields.deep = &pointer;
    fields.changing = bytes;
    fields.plain = 2;
    deep_global = &pointer;
    check("volatile member", vmember(&object, 0), 0x34 * 3);
    check("volatile word", vmword(&object), 0x3412);
    check("ordinary member", nmember(&ordinary, 0), 0x34 * 3);
    check("ordinary word", nmword(&ordinary), 0x3412);
    check("nested volatile", vnested(&nested, 0), 9);
    vmstore(&object, 0);
    check("volatile stores", object.bytes[1], 7);
    check("local volatile pointer", vptrptr(&pointer, 0), 9);
    check("indirect volatile pointer", vindirect(&pointer, 0), 9);
    check("typedef volatile pointer", vtypedef(&pointer, 0), 9);
    check("old-style volatile pointer", vold(&pointer, 0), 9);
    check("global volatile pointer", vglobal(0), 9);
    check("local volatile pointer copy", vlocal(&pointer, 0), 9);
    check("pointer field", vpfield(&fields, 0), 9);
    check("deep pointer field", vdfield(&fields, 0), 9);
    check("volatile pointer ordinary data", vchange(&fields, 0), 15);
    check("ordinary indirect pointer", nindirect(&ordinary_pointer, 0), 15);
    check("ordinary local pointer", nlocal(&ordinary_pointer, 0), 5);
    check("both pointer levels volatile", vboth(&changing_pointer, 0), 9);
    check("static pointer", vstatic(&pointer, 0), 9);
    check("alias store", aliaswrite(bytes, bytes + 1, 0), 19);
    bytes[1] = 5;
    bytes[2] = 0;
    check("distinct store", aliaswrite(bytes, bytes + 2, 0), 15);
    bytes[1] = 5;
    check("alias call", aliascall(bytes, bytes + 1, 0), 23);
    bytes[1] = 5;
    check("alias branch taken", abranch(bytes, bytes + 1, 0, 1), 27);
    bytes[1] = 5;
    check("alias branch skipped", abranch(bytes, bytes + 1, 0, 0), 15);
    bytes[1] = 5;
    check("alias copy", acopy(bytes, bytes + 1, 0), 31);
    printf("MIR alias failures=%d\n", failures);
    return failures != 0;
}