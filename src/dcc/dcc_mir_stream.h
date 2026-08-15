/*
 * dcc_mir_stream.h - portable in-memory scratch stream for MIR candidate
 * codegen, replacing tmpfile()-backed FILE* scratch streams.
 *
 * MirStream mimics the small subset of ISO C stdio semantics the MIR
 * backend's candidate-matrix codegen actually uses (sequential and
 * seek/rewind text writes and reads; never a real file, never platform
 * I/O). tmpfile() is cheap on Linux (anonymous tmpfs-backed) but touches
 * the real filesystem on every call on Windows, and the candidate matrix
 * creates one scratch stream per codegen attempt per function - this
 * type removes that filesystem traffic entirely, uniformly on every
 * platform.
 *
 * Every field of the underlying struct (defined privately in
 * dcc_mir_stream.c) is stdint.h/stddef.h fixed-width, so behavior is
 * identical on LP64 (Linux/macOS 64-bit), LLP64 (MSVC), and ILP32
 * (32-bit Linux). MirStream is opaque here; callers only ever hold a
 * MirStream*, exactly like FILE*.
 */

#ifndef DCC_MIR_STREAM_H
#define DCC_MIR_STREAM_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

typedef struct MirStream MirStream;

MirStream *mir_stream_open(void);
void mir_stream_close(MirStream *stream);

int mir_stream_printf(MirStream *stream, const char *format, ...);
int mir_stream_vprintf(MirStream *stream, const char *format, va_list args);
int mir_stream_puts(const char *s, MirStream *stream);
int mir_stream_putc(int c, MirStream *stream);

char *mir_stream_gets(char *buf, int size, MirStream *stream);
int mir_stream_getc(MirStream *stream);

int mir_stream_seek(MirStream *stream, long offset, int whence);
long mir_stream_tell(const MirStream *stream);
void mir_stream_rewind(MirStream *stream);

size_t mir_stream_write(const void *ptr, size_t size, size_t nmemb,
                         MirStream *stream);
size_t mir_stream_read(void *ptr, size_t size, size_t nmemb,
                        MirStream *stream);

/* Appends source's full content, from its start to its high-water mark,
 * onto destination at destination's current position. Source's own
 * position/content are left unchanged. */
void mir_stream_copy(MirStream *source, MirStream *destination);

/* Crosses into real-FILE* territory: writes source's full content to a
 * genuine FILE* and returns the running FNV-1a hash computed over those
 * exact bytes in the exact order written - the one boundary where
 * MirStream meets the real output file (fed by g_emit_sink.stream). */
unsigned long mir_stream_copy_to_file(MirStream *source, FILE *destination);

#endif
