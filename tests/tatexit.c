#include <stdio.h>
#include <stdlib.h>

/* Verify LIFO order and that all handlers run before output completes. */

#ifdef W27_FAST_CALLBACKS
extern void __fastcall h1(void);
extern void __fastcall h2(void);
extern void __fastcall h3(void);
extern void w27_callback_body(void);
extern volatile int w27_keep_callback_bodies;
#else
static void h1(void) { printf("h1\n"); }
static void h2(void) { printf("h2\n"); }
static void h3(void) { printf("h3\n"); }
#endif

#ifdef W27_FAST_REGISTER
extern int __fastcall w27_atexit(void (*callback)(void));
#define atexit w27_atexit
#elif defined(W27_FAIL_REGISTRATION)
static int w27_registration_count;

static int w27_atexit(void (*callback)(void))
{
    (void)callback;
    ++w27_registration_count;
    return w27_registration_count == W27_FAIL_REGISTRATION;
}
#define atexit w27_atexit
#endif

#ifdef W27_UNSIGNED_RESULTS
#define W27_RESULT_TYPE unsigned int
#elif defined(W27_VOLATILE_RESULTS)
#define W27_RESULT_TYPE volatile int
#else
#define W27_RESULT_TYPE int
#endif

/* Registration order: h1, h2, h3  ->  call order: h3, h2, h1 */
int main(void)
{
    W27_RESULT_TYPE r1, r2, r3;

    r1 = atexit(h1);
    r2 = atexit(h2);
    r3 = atexit(h3);

    if (r1 || r2 || r3)
        printf("FAIL atexit returned nonzero\n");
    else
        printf("tatexit ok\n");

#ifdef W27_FAST_CALLBACKS
    if (w27_keep_callback_bodies) {
        w27_callback_body();
    }
#endif
    return 0;
    /* h3, h2, h1 print after this */
}
