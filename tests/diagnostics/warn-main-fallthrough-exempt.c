/* main is exempt from the "control reaches end of non-void function" warning:
 * C supplies an implicit `return 0;` when control falls off the end of main,
 * so dcc must stay silent here. Any diagnostic output from this file means the
 * main-name exemption in dcc_stmt.c has regressed. Expected: clean compile,
 * no warnings (empty baseline). */
int main(void)
{
    int x;
    x = 3;
    x = x + 1;
}
