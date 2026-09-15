#include <stdio.h>

#ifdef WIDEHASH33_RENAMED
#define HASH_FUNCTION wide_hash33_renamed
#else
#define HASH_FUNCTION wide_hash33
#endif

#ifdef WIDEHASH33_VOLATILE_TEXT
#define HASH_TEXT const volatile char *
#else
#define HASH_TEXT const char *
#endif

#ifdef WIDEHASH33_RENAMED_LOCALS
#define TEXT_VALUE input_text
#define HASH_VALUE running_hash
#else
#define TEXT_VALUE text
#define HASH_VALUE hash
#endif

static unsigned long HASH_FUNCTION(HASH_TEXT TEXT_VALUE)
{
    unsigned long HASH_VALUE = 0;

#ifdef WIDEHASH33_ALT_FLOW
    while (*TEXT_VALUE != '\0') {
        unsigned char value = (unsigned char)*TEXT_VALUE;
        ++TEXT_VALUE;
        HASH_VALUE = HASH_VALUE * 33UL + value;
    }
#else
    while (*TEXT_VALUE != '\0')
        HASH_VALUE = HASH_VALUE * 33UL +
            (unsigned char)*TEXT_VALUE++;
#endif
    return HASH_VALUE;
}

int main(void)
{
    static const char high_bytes[] = {
        (char)0xff, (char)0x80, 'Z', '\0'
    };
    int failures = 0;
    unsigned long empty_hash = HASH_FUNCTION("");
    unsigned long abc_hash = HASH_FUNCTION("abc");
    unsigned long digest_hash = HASH_FUNCTION("message digest");
    unsigned long digits_hash = HASH_FUNCTION("0123456789");
    unsigned long high_hash = HASH_FUNCTION(high_bytes);

    if (empty_hash != 0UL)
        ++failures;
    if (abc_hash != 108966UL)
        ++failures;
    if (digest_hash != 2634864069UL)
        ++failures;
    if (digits_hash != 95184653UL)
        ++failures;
    if (high_hash != 282009UL)
        ++failures;
    printf("wide hash33 failures=%d values=%lu,%lu,%lu,%lu,%lu\n",
           failures, empty_hash, abc_hash, digest_hash,
           digits_hash, high_hash);
    return failures != 0;
}
