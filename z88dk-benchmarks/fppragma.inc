// Enables the printf() converters needed by floating-point test apps
// (mm.c, etc). Without this, zcc links a converter set that EXCLUDES
// floating point by default - %f/%e/%g silently print as literal text
// instead of formatting the value. Kept in its own include (used only by
// mf.bat) so ordinary non-fp apps built with m.bat keep the smaller,
// float-free default and don't pay for converters they don't use.
#pragma printf = "c s u d x X lu ld lx lX f e g"
