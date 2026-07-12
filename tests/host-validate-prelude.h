/*
 * host-validate-prelude.h - force-included by scripts/validate-unit-test.ps1
 * into every host build (never by dcc itself, which already provides these
 * directly in its own RTL).
 *
 * Supplies host-libc equivalents for CP/M-target RTL extensions dcc's own
 * tests use directly by their dcc name, so those tests can still be built
 * and cross-checked against a host compiler instead of being skipped
 * outright. Each entry here should be a case where the *behavior* is truly
 * equivalent, not a source-level workaround for a real target/host
 * difference (those belong in tests/_test_overrides.json's host/ignore
 * flags instead).
 */
#ifndef DCC_HOST_VALIDATE_PRELUDE_H
#define DCC_HOST_VALIDATE_PRELUDE_H

/* stricmp: ASCII case-insensitive strcmp. dcc's RTL provides it under this
 * exact (CP/M-derived) name. Neither glibc/BSD libc (POSIX strcasecmp,
 * <strings.h>) nor MSVC's conformant-mode CRT (_stricmp, <string.h> - the
 * unprefixed name is only available via legacy/non-standard-names opt-in)
 * expose "stricmp" itself. */
#if defined(_MSC_VER)
#include <string.h>
#define stricmp _stricmp
#else
#include <strings.h>
#define stricmp strcasecmp
#endif

#endif
