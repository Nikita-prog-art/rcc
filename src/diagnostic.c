#include "diagnostic.h"

#include <stdarg.h>

void diagnostic_sink_init(DiagnosticSink *sink, FILE *stream) {
    sink->stream = stream == NULL ? stderr : stream;
    sink->error_count = 0;
}

void diagnostic_error(DiagnosticSink *sink, SourceSpan span, const char *phase, const char *fmt, ...) {
    FILE *stream = stderr;
    va_list args;

    if (sink != NULL) {
        sink->error_count++;
        stream = sink->stream == NULL ? stderr : sink->stream;
    }

    if (span.line > 0) {
        fprintf(stream, "%s error at %zu:%zu: ", phase, span.line, span.column);
    } else {
        fprintf(stream, "%s error: ", phase);
    }

    va_start(args, fmt);
    vfprintf(stream, fmt, args);
    va_end(args);
    fputc('\n', stream);
}
