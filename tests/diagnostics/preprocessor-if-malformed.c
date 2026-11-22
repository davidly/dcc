/* Malformed #if and #elif expressions. */
#if
#endif
#if 0
#elif
#endif
#if 1 ==
#endif
#if 1)
#endif
#if 1 / 0
#endif
#if 1.5
#endif
#if "a"
#endif
#if X = 1
#endif
#if F(1)
#endif
#if defined
#endif
#if 1 ? 2
#endif
#if 09
#endif
#if (int)1
#endif

/* Not evaluated, so not an error. */
#if 0 && 1 / 0
#endif

int x;
