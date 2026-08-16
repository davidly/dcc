/**
 * @file dcc_mir_stream.c
 * @brief Implements the in-memory stream used for isolated MIR candidates.
 *
 * @par Role
 * Owns the growable buffer and the FILE-like read, write, seek, rewind, copy,
 * and final-file transfer operations declared in dcc_mir_stream.h.
 */

#include "dcc_mir_stream.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dcc.h"

struct MirStream {
    char   *data;
    size_t  length;    /* high-water mark of bytes written, like EOF position */
    size_t  capacity;  /* allocated size of data, including the NUL slot */
    size_t  position;  /* current read/write cursor, 0..length */
};

static void mir_stream_reserve(MirStream *stream, size_t need)
{
    size_t newcap;
    char *p;

    if (need <= stream->capacity)
        return;
    newcap = stream->capacity ? stream->capacity : 4096;
    while (newcap < need)
        newcap *= 2;
    p = (char *)realloc(stream->data, newcap);
    if (!p)
        fatal("out of memory");
    stream->data = p;
    stream->capacity = newcap;
}

MirStream *mir_stream_open(void)
{
    MirStream *stream = (MirStream *)malloc(sizeof(MirStream));
    if (!stream)
        return NULL;
    stream->data = NULL;
    stream->length = 0;
    stream->capacity = 0;
    stream->position = 0;
    mir_stream_reserve(stream, 1);
    stream->data[0] = 0;
    return stream;
}

void mir_stream_close(MirStream *stream)
{
    if (!stream)
        return;
    free(stream->data);
    free(stream);
}

/* Grows the buffer if needed so that [position, position+extra) is valid,
 * and extends length/keeps the buffer NUL-terminated at the new length. */
static void mir_stream_grow_for_write(MirStream *stream, size_t extra)
{
    mir_stream_reserve(stream, stream->position + extra + 1);
    if (stream->position + extra > stream->length) {
        stream->length = stream->position + extra;
        stream->data[stream->length] = 0;
    }
}

int mir_stream_vprintf(MirStream *stream, const char *format, va_list args)
{
    va_list copy;
    size_t avail;
    int n;

    va_copy(copy, args);
    avail = stream->capacity - stream->position;
    n = vsnprintf(stream->data + stream->position, avail, format, args);
    if (n < 0) {
        va_end(copy);
        return n;
    }
    if ((size_t)n >= avail) {
        mir_stream_reserve(stream, stream->position + (size_t)n + 1);
        avail = stream->capacity - stream->position;
        n = vsnprintf(stream->data + stream->position, avail, format, copy);
    }
    va_end(copy);
    stream->position += (size_t)n;
    if (stream->position > stream->length)
        stream->length = stream->position;
    return n;
}

int mir_stream_printf(MirStream *stream, const char *format, ...)
{
    va_list args;
    int n;

    va_start(args, format);
    n = mir_stream_vprintf(stream, format, args);
    va_end(args);
    return n;
}

int mir_stream_puts(const char *s, MirStream *stream)
{
    size_t n = strlen(s);
    mir_stream_grow_for_write(stream, n);
    memcpy(stream->data + stream->position, s, n);
    stream->position += n;
    return 0;
}

int mir_stream_putc(int c, MirStream *stream)
{
    mir_stream_grow_for_write(stream, 1);
    stream->data[stream->position] = (char)c;
    stream->position += 1;
    return (unsigned char)c;
}

char *mir_stream_gets(char *buf, int size, MirStream *stream)
{
    int i = 0;

    if (size <= 0 || stream->position >= stream->length)
        return NULL;
    while (i < size - 1 && stream->position < stream->length) {
        char c = stream->data[stream->position++];
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    buf[i] = 0;
    return buf;
}

int mir_stream_getc(MirStream *stream)
{
    if (stream->position >= stream->length)
        return EOF;
    return (unsigned char)stream->data[stream->position++];
}

int mir_stream_seek(MirStream *stream, long offset, int whence)
{
    long base;
    long target;

    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = (long)stream->position; break;
    case SEEK_END: base = (long)stream->length; break;
    default: return -1;
    }
    target = base + offset;
    if (target < 0)
        return -1;
    stream->position = (size_t)target;
    return 0;
}

long mir_stream_tell(const MirStream *stream)
{
    return (long)stream->position;
}

void mir_stream_rewind(MirStream *stream)
{
    stream->position = 0;
}

size_t mir_stream_write(const void *ptr, size_t size, size_t nmemb,
                         MirStream *stream)
{
    size_t bytes = size * nmemb;
    if (bytes == 0)
        return 0;
    mir_stream_grow_for_write(stream, bytes);
    memcpy(stream->data + stream->position, ptr, bytes);
    stream->position += bytes;
    return nmemb;
}

size_t mir_stream_read(void *ptr, size_t size, size_t nmemb, MirStream *stream)
{
    size_t avail = stream->length - stream->position;
    size_t items;

    if (size == 0 || nmemb == 0)
        return 0;
    items = nmemb;
    if (items * size > avail)
        items = avail / size;
    if (items == 0)
        return 0;
    memcpy(ptr, stream->data + stream->position, items * size);
    stream->position += items * size;
    return items;
}

void mir_stream_copy(MirStream *source, MirStream *destination)
{
    mir_stream_grow_for_write(destination, source->length);
    memcpy(destination->data + destination->position, source->data,
           source->length);
    destination->position += source->length;
}

unsigned long mir_stream_copy_to_file(MirStream *source, FILE *destination)
{
    unsigned long hash = 2166136261UL;
    size_t i;

    if (source->length > 0 &&
        fwrite(source->data, 1, source->length, destination) != source->length)
        fatal("cannot write MIR selected stream");
    for (i = 0; i < source->length; i++) {
        hash ^= (unsigned long)(unsigned char)source->data[i];
        hash = (hash * 16777619UL) & 0xffffffffUL;
    }
    return hash;
}
