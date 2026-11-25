/* A malformed designator on a bit-field is reported once; the leftover
 * `-- 7` must not send lowering into endless recursion (syntax-crash seed
 * 1196576). */
struct V { int major : 4; };

int main(void)
{
    struct V v = { .major -- 7 };
    return 0;
}
