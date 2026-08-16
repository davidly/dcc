/**
 * @file dcc_mir_stream.h
 * @brief Declares the portable in-memory stream for MIR candidate output.
 *
 * @par Role
 * MirStream supplies the small sequential and seekable stdio subset needed by
 * candidate emitters without touching the filesystem. Candidate attempts stay
 * isolated until selection; mir_stream_copy_to_file() is the sole boundary to
 * the compiler's real FILE output.
 *
 * @par Design
 * The type is opaque and implemented in dcc_mir_stream.c. Its byte-oriented
 * state has identical semantics on LP64, LLP64, and ILP32 hosts.
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
