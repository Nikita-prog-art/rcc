#ifndef RCC_LEXER_H
#define RCC_LEXER_H

#include "token.h"

typedef struct Lexer {
    const char *source;
    size_t length;
    size_t offset;
    size_t line;
    size_t column;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token lexer_next(Lexer *lexer);

#endif
