#ifndef RCC_PARSER_H
#define RCC_PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct Parser {
    Lexer lexer;
    Token current;
    Token next;
    bool has_error;
} Parser;

void parser_init(Parser *parser, const char *source);
Program *parse_program(Parser *parser);

#endif
