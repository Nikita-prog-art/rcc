#ifndef RCC_DIAGNOSTIC_H
#define RCC_DIAGNOSTIC_H

#include "common.h"

#include <stdio.h>

typedef struct DiagnosticSink {
    FILE *stream;
    size_t error_count;
} DiagnosticSink;

void diagnostic_sink_init(DiagnosticSink *sink, FILE *stream);
void diagnostic_error(DiagnosticSink *sink, SourceSpan span, const char *phase, const char *fmt, ...);

#endif
