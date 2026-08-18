# Signal handling (`signal.h`)

Include [`signal.h`](16-signal.md) for signal constants and abnormal program
termination.

## Types and Macros

<!-- SIGNAL-SYMBOL-TABLE: all -->

The signal numbers and handler constants provide C89 source compatibility.
CP/M does not install or deliver asynchronous signal handlers.

## Functions

<!-- SIGNAL-FUNCTION-TABLE: all -->

## Runtime model

`signal()` always returns `SIG_ERR`; neither `SIG_DFL`, `SIG_IGN`, nor a
user-provided handler can be installed on CP/M 2.2.

`raise(SIGABRT)` calls `abort()` and does not return. Raising any other defined
signal is a no-op that returns zero.
