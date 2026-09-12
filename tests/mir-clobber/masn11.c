#include <stdio.h>

struct Sample {
    unsigned char left;
    signed char signed_byte;
    unsigned char unsigned_byte;
    int signed_word;
    unsigned int unsigned_word;
    unsigned char right;
};

static struct Sample global_sample;
static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++failures;
    }
}

int main(void)
{
    struct Sample local_sample;
    struct Sample * const qualified_pointer = &local_sample;
    int word_result;
    int byte_result;

    global_sample.left = 0x55;
    global_sample.right = 0xaa;
    global_sample.signed_byte = -3L;
    global_sample.unsigned_byte = 259L;
    global_sample.signed_word = -7.75f;
    global_sample.unsigned_word = 259.75f;

    check(global_sample.signed_byte == -3, "global signed byte");
    check(global_sample.unsigned_byte == 3, "global unsigned byte");
    check(global_sample.signed_word == -7, "global signed word");
    check(global_sample.unsigned_word == 259, "global unsigned word");
    check(global_sample.left == 0x55 && global_sample.right == 0xaa,
          "global neighbors");

    local_sample.left = 0x33;
    local_sample.right = 0xcc;
    qualified_pointer->signed_byte = -4.75f;
    qualified_pointer->unsigned_byte = 4.75f;
    qualified_pointer->signed_word = -8L;
    qualified_pointer->unsigned_word = 260L;

    check(local_sample.signed_byte == -4, "qualified signed byte");
    check(local_sample.unsigned_byte == 4, "qualified unsigned byte");
    check(local_sample.signed_word == -8, "qualified signed word");
    check(local_sample.unsigned_word == 260, "qualified unsigned word");
    check(local_sample.left == 0x33 && local_sample.right == 0xcc,
          "qualified neighbors");

    word_result = (qualified_pointer->signed_word = -9.75f);
    byte_result = (qualified_pointer->signed_byte = -5L);
    check(word_result == -9 && local_sample.signed_word == -9,
          "word assignment result");
    check(byte_result == -5 && local_sample.signed_byte == -5,
          "byte assignment result");
    check(local_sample.left == 0x33 && local_sample.right == 0xcc,
          "result neighbors");

    printf("member numeric assignment failures=%d\n", failures);
    return failures != 0;
}
