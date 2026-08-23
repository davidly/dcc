// Minimal compatibility shim for building x80.cxx (the 8080/Z80 core from
// ntvcm) inside this repo. The upstream djltrace.hxx implements a file-backed
// tracer; the x80 core only touches tracer.IsEnabled() / tracer.Trace() on the
// (disabled) instruction-trace path, so a no-op stub is sufficient here.

#pragma once

#include <stdint.h>

class CDJLTrace
{
public:
    CDJLTrace() {}
    bool IsEnabled() { return false; }
    void Trace( const char *, ... ) {}
    void TraceQuiet( const char *, ... ) {}
    void TraceBinaryData( uint8_t *, uint32_t, uint32_t ) {}
};

extern CDJLTrace tracer;
