#include "parser.h"

#include <stdio.h>
#include <string.h>

static void parser_advance(Parser *parser) {
    parser->current = lexer_next(&parser->lexer);
    if (parser->current.kind == TOKEN_ERROR) {
        parser->has_error = true;
    }
}

static bool parser_expect(Parser *parser, TokenKind kind, const char *what) {
    if (parser->current.kind == kind) {
        parser_advance(parser);
        return true;
    }
    fprintf(stderr,
            "parse error at %zu:%zu: expected %s, found %s\n",
            parser->current.line,
            parser->current.column,
            what,
            token_kind_name(parser->current.kind));
    parser->has_error = true;
    return false;
}

static bool parse_type(Parser *parser, TypeKind *out_type) {
    if (parser->current.kind != TOKEN_I32) {
        fprintf(stderr,
                "parse error at %zu:%zu: only i32 is supported\n",
                parser->current.line,
                parser->current.column);
        parser->has_error = true;
        return false;
    }
    *out_type = TYPE_I32;
    parser_advance(parser);
    return true;
}

static Expr *parse_expr(Parser *parser);
static Expr *parse_call(Parser *parser, Token callee);

static Expr *parse_primary(Parser *parser) {
    Token token = parser->current;
    if (token.kind == TOKEN_INTEGER) {
        parser_advance(parser);
        return expr_create_integer(token.integer_value);
    }
    if (token.kind == TOKEN_IDENTIFIER) {
        parser_advance(parser);
        if (parser->current.kind == TOKEN_LPAREN) {
            return parse_call(parser, token);
        }
        return expr_create_name(token.lexeme, token.length);
    }
    if (token.kind == TOKEN_LPAREN) {
        parser_advance(parser);
        Expr *expr = parse_expr(parser);
        if (!parser_expect(parser, TOKEN_RPAREN, "')'")) {
            return NULL;
        }
        return expr;
    }

    fprintf(stderr,
            "parse error at %zu:%zu: expected expression, found %s\n",
            token.line,
            token.column,
            token_kind_name(token.kind));
    parser->has_error = true;
    return NULL;
}

static Expr *parse_call(Parser *parser, Token callee) {
    Expr *call = expr_create_call(callee.lexeme, callee.length);
    if (!parser_expect(parser, TOKEN_LPAREN, "'('")) {
        return NULL;
    }
    while (!parser->has_error && parser->current.kind != TOKEN_RPAREN) {
        Expr *arg = parse_expr(parser);
        if (arg == NULL) {
            return NULL;
        }
        expr_append_call_arg(call, arg);
        if (parser->current.kind == TOKEN_COMMA) {
            parser_advance(parser);
            continue;
        }
        break;
    }
    if (!parser_expect(parser, TOKEN_RPAREN, "')'")) {
        return NULL;
    }
    return call;
}

static Expr *parse_multiplicative(Parser *parser) {
    Expr *expr = parse_primary(parser);
    while (!parser->has_error &&
           (parser->current.kind == TOKEN_STAR || parser->current.kind == TOKEN_SLASH)) {
        TokenKind op = parser->current.kind;
        parser_advance(parser);
        Expr *rhs = parse_primary(parser);
        if (rhs == NULL) {
            return NULL;
        }
        expr = expr_create_binary(op == TOKEN_STAR ? BINARY_MUL : BINARY_DIV, expr, rhs);
    }
    return expr;
}

static Expr *parse_additive(Parser *parser) {
    Expr *expr = parse_multiplicative(parser);
    while (!parser->has_error &&
           (parser->current.kind == TOKEN_PLUS || parser->current.kind == TOKEN_MINUS)) {
        TokenKind op = parser->current.kind;
        parser_advance(parser);
        Expr *rhs = parse_multiplicative(parser);
        if (rhs == NULL) {
            return NULL;
        }
        expr = expr_create_binary(op == TOKEN_PLUS ? BINARY_ADD : BINARY_SUB, expr, rhs);
    }
    return expr;
}

static Expr *parse_expr(Parser *parser) {
    return parse_additive(parser);
}

static Stmt *parse_statement(Parser *parser) {
    if (parser->current.kind == TOKEN_LET) {
        parser_advance(parser);
        Token name = parser->current;
        if (!parser_expect(parser, TOKEN_IDENTIFIER, "identifier")) {
            return NULL;
        }
        if (!parser_expect(parser, TOKEN_COLON, "':'")) {
            return NULL;
        }
        TypeKind type;
        if (!parse_type(parser, &type)) {
            return NULL;
        }
        if (!parser_expect(parser, TOKEN_EQUAL, "'='")) {
            return NULL;
        }
        Expr *value = parse_expr(parser);
        if (value == NULL) {
            return NULL;
        }
        if (!parser_expect(parser, TOKEN_SEMICOLON, "';'")) {
            return NULL;
        }
        return stmt_create_let(name.lexeme, name.length, type, value);
    }

    if (parser->current.kind == TOKEN_RETURN) {
        parser_advance(parser);
        Expr *value = parse_expr(parser);
        if (value == NULL) {
            return NULL;
        }
        if (!parser_expect(parser, TOKEN_SEMICOLON, "';'")) {
            return NULL;
        }
        return stmt_create_return(value);
    }

    fprintf(stderr,
            "parse error at %zu:%zu: expected statement, found %s\n",
            parser->current.line,
            parser->current.column,
            token_kind_name(parser->current.kind));
    parser->has_error = true;
    return NULL;
}

static Function *parse_function(Parser *parser) {
    if (!parser_expect(parser, TOKEN_FN, "'fn'")) {
        return NULL;
    }

    Token name = parser->current;
    if (!parser_expect(parser, TOKEN_IDENTIFIER, "function name")) {
        return NULL;
    }
    if (!parser_expect(parser, TOKEN_LPAREN, "'('")) {
        return NULL;
    }
    Function *function = function_create(name.lexeme, name.length, TYPE_I32);
    while (!parser->has_error && parser->current.kind != TOKEN_RPAREN) {
        Token param_name = parser->current;
        if (!parser_expect(parser, TOKEN_IDENTIFIER, "parameter name")) {
            return function;
        }
        if (!parser_expect(parser, TOKEN_COLON, "':'")) {
            return function;
        }
        TypeKind param_type;
        if (!parse_type(parser, &param_type)) {
            return function;
        }
        function_append_param(function, param_name.lexeme, param_name.length, param_type);
        if (parser->current.kind == TOKEN_COMMA) {
            parser_advance(parser);
            continue;
        }
        break;
    }
    if (!parser_expect(parser, TOKEN_RPAREN, "')'")) {
        return function;
    }
    if (!parser_expect(parser, TOKEN_ARROW, "'->'")) {
        return function;
    }

    TypeKind return_type;
    if (!parse_type(parser, &return_type)) {
        return function;
    }
    function->return_type = return_type;
    if (!parser_expect(parser, TOKEN_LBRACE, "'{'")) {
        return function;
    }

    while (!parser->has_error && parser->current.kind != TOKEN_RBRACE && parser->current.kind != TOKEN_EOF) {
        Stmt *statement = parse_statement(parser);
        if (statement == NULL) {
            return function;
        }
        function_append_statement(function, statement);
    }

    if (!parser_expect(parser, TOKEN_RBRACE, "'}'")) {
        return function;
    }
    return function;
}

void parser_init(Parser *parser, const char *source) {
    lexer_init(&parser->lexer, source);
    parser->has_error = false;
    parser_advance(parser);
}

Program *parse_program(Parser *parser) {
    Program *program = program_create();
    while (!parser->has_error && parser->current.kind != TOKEN_EOF) {
        Function *function = parse_function(parser);
        if (function == NULL) {
            break;
        }
        program_append_function(program, function);
    }

    if (parser->has_error) {
        program_destroy(program);
        return NULL;
    }
    return program;
}
