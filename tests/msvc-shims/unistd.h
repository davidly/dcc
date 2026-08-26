/*
 * unistd.h - minimal MSVC compatibility shim, force-searched only by
 * scripts/validate-unit-test.ps1's MSVC host build (via /I), never by dcc
 * itself or by gcc/clang builds, which have a real <unistd.h>.
 *
 * dcc's RTL exposes a handful of POSIX <unistd.h> names directly (its tests
 * use them by those names), but MSVC has no <unistd.h> at all. This supplies
 * just enough of it - one declaration per name actually used by a test - via
 * MSVC's own CRT equivalents, so those tests build instead of being skipped
 * outright. Add to this file only when a *behaviorally equivalent* MSVC CRT
 * function exists; a real host/target difference belongs in
 * tests/_test_overrides.json's host/ignore flags instead.
 */
#ifndef DCC_HOST_VALIDATE_UNISTD_H
#define DCC_HOST_VALIDATE_UNISTD_H

#include <io.h>

/* unlink: delete a file. MSVC's CRT provides the identical operation as
 * _unlink(). */
#define unlink _unlink

#endif
