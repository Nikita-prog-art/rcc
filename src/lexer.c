#include "lexer.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char lexer_peek(const Lexer *lexer) {
    if (lexer->offset >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->offset];
}

static char lexer_peek_next(const Lexer *lexer) {
    if (lexer->offset + 1 >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->offset + 1];
}

static char lexer_advance(Lexer *lexer) {
    char ch = lexer_peek(lexer);
    if (ch == '\0') {
        return ch;
    }
    lexer->offset++;
    if (ch == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return ch;
}

static Token make_token(const Lexer *lexer, TokenKind kind, const char *start, size_t length) {
    Token token;
    token.kind = kind;
    token.lexeme = start;
    token.length = length;
    token.line = lexer->line;
    token.column = lexer->column;
    token.integer_value = 0;
    return token;
}

static void skip_whitespace(Lexer *lexer) {
    for (;;) {
        char ch = lexer_peek(lexer);
        if (isspace((unsigned char) ch)) {
            lexer_advance(lexer);
            continue;
        }
        if (ch == '/' && lexer_peek_next(lexer) == '/') {
            while (lexer_peek(lexer) != '\n' && lexer_peek(lexer) != '\0') {
                lexer_advance(lexer);
            }
            continue;
        }
        if (ch == '/' && lexer_peek_next(lexer) == '*') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            bool closed = false;
            while (lexer_peek(lexer) != '\0') {
                if (lexer_peek(lexer) == '*' && lexer_peek_next(lexer) == '/') {
                    lexer_advance(lexer);
                    lexer_advance(lexer);
                    closed = true;
                    break;
                }
                lexer_advance(lexer);
            }
            if (!closed) {
                fprintf(stderr, "lexer error at %zu:%zu: unterminated block comment\n", lexer->line, lexer->column);
                lexer->has_error = true;
                lexer->error_line = lexer->line;
                lexer->error_column = lexer->column;
                return;
            }
            continue;
        }
        break;
    }
}

static TokenKind identifier_kind(const char *start, size_t length) {
    if (length == 2 && strncmp(start, "fn", 2) == 0) {
        return TOKEN_FN;
    }
    if (length == 3 && strncmp(start, "let", 3) == 0) {
        return TOKEN_LET;
    }
    if (length == 6 && strncmp(start, "return", 6) == 0) {
        return TOKEN_RETURN;
    }
    if (length == 2 && strncmp(start, "if", 2) == 0) {
        return TOKEN_IF;
    }
    if (length == 4 && strncmp(start, "else", 4) == 0) {
        return TOKEN_ELSE;
    }
    if (length == 5 && strncmp(start, "while", 5) == 0) {
        return TOKEN_WHILE;
    }
    if (length == 4 && strncmp(start, "loop", 4) == 0) {
        return TOKEN_LOOP;
    }
    if (length == 3 && strncmp(start, "mut", 3) == 0) {
        return TOKEN_MUT;
    }
    if (length == 5 && strncmp(start, "break", 5) == 0) {
        return TOKEN_BREAK;
    }
    if (length == 8 && strncmp(start, "continue", 8) == 0) {
        return TOKEN_CONTINUE;
    }
    if (length == 3 && strncmp(start, "i32", 3) == 0) {
        return TOKEN_I32;
    }
    return TOKEN_IDENTIFIER;
}

void lexer_init(Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->length = strlen(source);
    lexer->offset = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->has_error = false;
    lexer->error_line = 1;
    lexer->error_column = 1;
}

Token lexer_next(Lexer *lexer) {
    skip_whitespace(lexer);

    if (lexer->has_error) {
        Token token = make_token(lexer, TOKEN_ERROR, lexer->source + lexer->offset, 0);
        token.line = lexer->error_line;
        token.column = lexer->error_column;
        return token;
    }

    const char *start = lexer->source + lexer->offset;
    size_t line = lexer->line;
    size_t column = lexer->column;
    char ch = lexer_advance(lexer);

    if (ch == '\0') {
        Token token = make_token(lexer, TOKEN_EOF, start, 0);
        token.line = line;
        token.column = column;
        return token;
    }

    if (isalpha((unsigned char) ch) || ch == '_') {
        while (isalnum((unsigned char) lexer_peek(lexer)) || lexer_peek(lexer) == '_') {
            lexer_advance(lexer);
        }
        size_t length = (size_t) ((lexer->source + lexer->offset) - start);
        Token token = make_token(lexer, identifier_kind(start, length), start, length);
        token.line = line;
        token.column = column;
        return token;
    }

    if (isdigit((unsigned char) ch)) {
        long value = ch - '0';
        while (isdigit((unsigned char) lexer_peek(lexer))) {
            int digit = lexer_advance(lexer) - '0';
            if (value > (INT32_MAX - digit) / 10) {
                fprintf(stderr, "lexer error at %zu:%zu: integer literal overflow\n", line, column);
                Token error = make_token(lexer, TOKEN_ERROR, start, (size_t) ((lexer->source + lexer->offset) - start));
                error.line = line;
                error.column = column;
                return error;
            }
            value = value * 10 + digit;
        }
        size_t length = (size_t) ((lexer->source + lexer->offset) - start);
        Token token = make_token(lexer, TOKEN_INTEGER, start, length);
        token.line = line;
        token.column = column;
        token.integer_value = value;
        return token;
    }

    Token token = make_token(lexer, TOKEN_ERROR, start, 1);
    token.line = line;
    token.column = column;

    switch (ch) {
        case '(':
            token.kind = TOKEN_LPAREN;
            break;
        case ')':
            token.kind = TOKEN_RPAREN;
            break;
        case '{':
            token.kind = TOKEN_LBRACE;
            break;
        case '}':
            token.kind = TOKEN_RBRACE;
            break;
        case ':':
            token.kind = TOKEN_COLON;
            break;
        case ';':
            token.kind = TOKEN_SEMICOLON;
            break;
        case ',':
            token.kind = TOKEN_COMMA;
            break;
        case '=':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                token.kind = TOKEN_EQUAL_EQUAL;
                token.length = 2;
            } else {
                token.kind = TOKEN_EQUAL;
            }
            break;
        case '!':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                token.kind = TOKEN_BANG_EQUAL;
                token.length = 2;
            } else {
                token.kind = TOKEN_BANG;
            }
            break;
        case '<':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                token.kind = TOKEN_LESS_EQUAL;
                token.length = 2;
            } else {
                token.kind = TOKEN_LESS;
            }
            break;
        case '>':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                token.kind = TOKEN_GREATER_EQUAL;
                token.length = 2;
            } else {
                token.kind = TOKEN_GREATER;
            }
            break;
        case '+':
            token.kind = TOKEN_PLUS;
            break;
        case '*':
            token.kind = TOKEN_STAR;
            break;
        case '%':
            token.kind = TOKEN_PERCENT;
            break;
        case '/':
            token.kind = TOKEN_SLASH;
            break;
        case '-':
            if (lexer_peek(lexer) == '>') {
                lexer_advance(lexer);
                token.kind = TOKEN_ARROW;
                token.length = 2;
            } else {
                token.kind = TOKEN_MINUS;
            }
            break;
        default:
            fprintf(stderr, "lexer error at %zu:%zu: unexpected character '%c'\n", line, column, ch);
            break;
    }

    return token;
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOKEN_EOF:
            return "eof";
        case TOKEN_ERROR:
            return "error";
        case TOKEN_IDENTIFIER:
            return "identifier";
        case TOKEN_INTEGER:
            return "integer";
        case TOKEN_FN:
            return "fn";
        case TOKEN_LET:
            return "let";
        case TOKEN_RETURN:
            return "return";
        case TOKEN_IF:
            return "if";
        case TOKEN_ELSE:
            return "else";
        case TOKEN_WHILE:
            return "while";
        case TOKEN_LOOP:
            return "loop";
        case TOKEN_MUT:
            return "mut";
        case TOKEN_BREAK:
            return "break";
        case TOKEN_CONTINUE:
            return "continue";
        case TOKEN_I32:
            return "i32";
        case TOKEN_LPAREN:
            return "(";
        case TOKEN_RPAREN:
            return ")";
        case TOKEN_LBRACE:
            return "{";
        case TOKEN_RBRACE:
            return "}";
        case TOKEN_COLON:
            return ":";
        case TOKEN_SEMICOLON:
            return ";";
        case TOKEN_COMMA:
            return ",";
        case TOKEN_ARROW:
            return "->";
        case TOKEN_EQUAL:
            return "=";
        case TOKEN_BANG:
            return "!";
        case TOKEN_EQUAL_EQUAL:
            return "==";
        case TOKEN_BANG_EQUAL:
            return "!=";
        case TOKEN_LESS:
            return "<";
        case TOKEN_LESS_EQUAL:
            return "<=";
        case TOKEN_GREATER:
            return ">";
        case TOKEN_GREATER_EQUAL:
            return ">=";
        case TOKEN_PLUS:
            return "+";
        case TOKEN_MINUS:
            return "-";
        case TOKEN_STAR:
            return "*";
        case TOKEN_PERCENT:
            return "%";
        case TOKEN_SLASH:
            return "/";
    }
    return "unknown";
}
