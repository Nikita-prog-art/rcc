#ifndef RCC_TOKEN_H
#define RCC_TOKEN_H

#include "common.h"

typedef enum TokenKind {
    TOKEN_EOF = 0,
    TOKEN_ERROR,
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_FN,
    TOKEN_LET,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_MUT,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_I32,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_ARROW,
    TOKEN_EQUAL,
    TOKEN_BANG,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_PERCENT,
    TOKEN_SLASH
} TokenKind;

typedef struct Token {
    TokenKind kind;
    const char *lexeme;
    size_t length;
    size_t line;
    size_t column;
    long integer_value;
} Token;

const char *token_kind_name(TokenKind kind);

#endif
