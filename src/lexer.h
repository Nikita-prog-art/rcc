#ifndef RCC_LEXER_H
#define RCC_LEXER_H

#include "diagnostic.h"
#include "token.h"

typedef struct Lexer {
    const char *source;
    size_t length;
    size_t offset;
    size_t line;
    size_t column;
    bool has_error;
    size_t error_line;
    size_t error_column;
    DiagnosticSink *diagnostics;
} Lexer;

void lexer_init(Lexer *lexer, const char *source, DiagnosticSink *diagnostics);
Token lexer_next(Lexer *lexer);

#endif
