/* A function-like macro in #if is expanded, so a wrong call is diagnosed. */
#define VER(major, minor) ((major) * 100 + (minor))

#if VER(1) > 100
#endif
#if VER(1, 2, 3) > 100
#endif

int x;
