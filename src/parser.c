#include "parser.h"

#include <stdio.h>
#include <string.h>

static void parser_advance(Parser *parser) {
    parser->current = parser->next;
    parser->next = lexer_next(&parser->lexer);
    if (parser->current.kind == TOKEN_ERROR || parser->next.kind == TOKEN_ERROR) {
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
static Expr *parse_primary(Parser *parser);
static Stmt *parse_statement(Parser *parser);
static Stmt *parse_if_statement(Parser *parser);

static bool token_starts_expr(TokenKind kind) {
    return kind == TOKEN_INTEGER ||
           kind == TOKEN_IDENTIFIER ||
           kind == TOKEN_LPAREN ||
           kind == TOKEN_PLUS ||
           kind == TOKEN_MINUS ||
           kind == TOKEN_BANG;
}

typedef enum BlockKind {
    BLOCK_IF_THEN = 0,
    BLOCK_IF_ELSE,
    BLOCK_WHILE_BODY,
    BLOCK_LOOP_BODY,
    BLOCK_STANDALONE
} BlockKind;

static bool parse_block(Parser *parser, Stmt *owner, BlockKind block_kind) {
    if (!parser_expect(parser, TOKEN_LBRACE, "'{'")) {
        return false;
    }
    while (!parser->has_error && parser->current.kind != TOKEN_RBRACE && parser->current.kind != TOKEN_EOF) {
        Stmt *statement = parse_statement(parser);
        if (statement == NULL) {
            return false;
        }
        if (block_kind == BLOCK_IF_THEN) {
            stmt_append_then_statement(owner, statement);
        } else if (block_kind == BLOCK_IF_ELSE) {
            stmt_append_else_statement(owner, statement);
        } else if (block_kind == BLOCK_WHILE_BODY) {
            stmt_append_while_statement(owner, statement);
        } else if (block_kind == BLOCK_LOOP_BODY) {
            stmt_append_loop_statement(owner, statement);
        } else {
            stmt_append_block_statement(owner, statement);
        }
    }
    return parser_expect(parser, TOKEN_RBRACE, "'}'");
}

static bool parse_else_branch(Parser *parser, Stmt *if_stmt) {
    if (parser->current.kind == TOKEN_IF) {
        Stmt *nested_if = parse_if_statement(parser);
        if (nested_if == NULL) {
            return false;
        }
        stmt_append_else_statement(if_stmt, nested_if);
        return true;
    }
    return parse_block(parser, if_stmt, BLOCK_IF_ELSE);
}

static Expr *parse_unary(Parser *parser) {
    if (parser->current.kind == TOKEN_PLUS) {
        parser_advance(parser);
        return parse_unary(parser);
    }

    if (parser->current.kind == TOKEN_MINUS) {
        parser_advance(parser);
        Expr *operand = parse_unary(parser);
        if (operand == NULL) {
            return NULL;
        }
        return expr_create_unary(UNARY_NEG, operand);
    }

    if (parser->current.kind == TOKEN_BANG) {
        parser_advance(parser);
        Expr *operand = parse_unary(parser);
        if (operand == NULL) {
            return NULL;
        }
        return expr_create_unary(UNARY_NOT, operand);
    }

    return parse_primary(parser);
}

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
            if (parser->current.kind == TOKEN_RPAREN) {
                break;
            }
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
    Expr *expr = parse_unary(parser);
    while (!parser->has_error &&
           (parser->current.kind == TOKEN_STAR ||
            parser->current.kind == TOKEN_SLASH ||
            parser->current.kind == TOKEN_PERCENT)) {
        TokenKind op = parser->current.kind;
        parser_advance(parser);
        Expr *rhs = parse_unary(parser);
        if (rhs == NULL) {
            return NULL;
        }
        BinaryOp binary_op = BINARY_MUL;
        switch (op) {
            case TOKEN_STAR:
                binary_op = BINARY_MUL;
                break;
            case TOKEN_SLASH:
                binary_op = BINARY_DIV;
                break;
            case TOKEN_PERCENT:
                binary_op = BINARY_REM;
                break;
            default:
                break;
        }
        expr = expr_create_binary(binary_op, expr, rhs);
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

static Expr *parse_comparison(Parser *parser) {
    Expr *expr = parse_additive(parser);
    while (!parser->has_error &&
           (parser->current.kind == TOKEN_LESS ||
            parser->current.kind == TOKEN_LESS_EQUAL ||
            parser->current.kind == TOKEN_GREATER ||
            parser->current.kind == TOKEN_GREATER_EQUAL)) {
        TokenKind op = parser->current.kind;
        parser_advance(parser);
        Expr *rhs = parse_additive(parser);
        if (rhs == NULL) {
            return NULL;
        }
        BinaryOp binary_op = BINARY_LT;
        switch (op) {
            case TOKEN_LESS:
                binary_op = BINARY_LT;
                break;
            case TOKEN_LESS_EQUAL:
                binary_op = BINARY_LE;
                break;
            case TOKEN_GREATER:
                binary_op = BINARY_GT;
                break;
            case TOKEN_GREATER_EQUAL:
                binary_op = BINARY_GE;
                break;
            default:
                break;
        }
        expr = expr_create_binary(binary_op, expr, rhs);
    }
    return expr;
}

static Expr *parse_equality(Parser *parser) {
    Expr *expr = parse_comparison(parser);
    while (!parser->has_error &&
           (parser->current.kind == TOKEN_EQUAL_EQUAL || parser->current.kind == TOKEN_BANG_EQUAL)) {
        TokenKind op = parser->current.kind;
        parser_advance(parser);
        Expr *rhs = parse_comparison(parser);
        if (rhs == NULL) {
            return NULL;
        }
        expr = expr_create_binary(op == TOKEN_EQUAL_EQUAL ? BINARY_EQ : BINARY_NE, expr, rhs);
    }
    return expr;
}

static Expr *parse_expr(Parser *parser) {
    return parse_equality(parser);
}

static Stmt *parse_if_statement(Parser *parser) {
    parser_advance(parser);
    Expr *condition = parse_expr(parser);
    if (condition == NULL) {
        return NULL;
    }
    Stmt *if_stmt = stmt_create_if(condition);
    if (!parse_block(parser, if_stmt, BLOCK_IF_THEN)) {
        return NULL;
    }
    if (parser->current.kind == TOKEN_ELSE) {
        parser_advance(parser);
        if (!parse_else_branch(parser, if_stmt)) {
            return NULL;
        }
    }
    return if_stmt;
}

static Stmt *parse_statement(Parser *parser) {
    if (parser->current.kind == TOKEN_SEMICOLON) {
        parser_advance(parser);
        return stmt_create_empty();
    }

    if (parser->current.kind == TOKEN_LET) {
        parser_advance(parser);
        bool is_mutable = false;
        if (parser->current.kind == TOKEN_MUT) {
            is_mutable = true;
            parser_advance(parser);
        }
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
        return stmt_create_let(name.lexeme, name.length, type, is_mutable, value);
    }

    if (parser->current.kind == TOKEN_IDENTIFIER && parser->next.kind == TOKEN_EQUAL) {
        Token name = parser->current;
        parser_advance(parser);
        parser_advance(parser);
        Expr *value = parse_expr(parser);
        if (value == NULL) {
            return NULL;
        }
        if (!parser_expect(parser, TOKEN_SEMICOLON, "';'")) {
            return NULL;
        }
        return stmt_create_assign(name.lexeme, name.length, value);
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

    if (parser->current.kind == TOKEN_BREAK) {
        parser_advance(parser);
        if (!parser_expect(parser, TOKEN_SEMICOLON, "';'")) {
            return NULL;
        }
        return stmt_create_break();
    }

    if (parser->current.kind == TOKEN_CONTINUE) {
        parser_advance(parser);
        if (!parser_expect(parser, TOKEN_SEMICOLON, "';'")) {
            return NULL;
        }
        return stmt_create_continue();
    }

    if (parser->current.kind == TOKEN_IF) {
        return parse_if_statement(parser);
    }

    if (parser->current.kind == TOKEN_WHILE) {
        parser_advance(parser);
        Expr *condition = parse_expr(parser);
        if (condition == NULL) {
            return NULL;
        }
        Stmt *while_stmt = stmt_create_while(condition);
        if (!parse_block(parser, while_stmt, BLOCK_WHILE_BODY)) {
            return NULL;
        }
        return while_stmt;
    }

    if (parser->current.kind == TOKEN_LOOP) {
        parser_advance(parser);
        Stmt *loop_stmt = stmt_create_loop();
        if (!parse_block(parser, loop_stmt, BLOCK_LOOP_BODY)) {
            return NULL;
        }
        return loop_stmt;
    }

    if (parser->current.kind == TOKEN_LBRACE) {
        Stmt *block_stmt = stmt_create_block();
        if (!parse_block(parser, block_stmt, BLOCK_STANDALONE)) {
            return NULL;
        }
        return block_stmt;
    }

    if (token_starts_expr(parser->current.kind)) {
        bool starts_with_identifier = parser->current.kind == TOKEN_IDENTIFIER;
        Expr *value = parse_expr(parser);
        if (value == NULL) {
            return NULL;
        }
        if (parser->current.kind == TOKEN_SEMICOLON) {
            fprintf(stderr,
                    "parse error at %zu:%zu: expression statements must currently be final in a block\n",
                    parser->current.line,
                    parser->current.column);
            parser->has_error = true;
            return NULL;
        }
        if (parser->current.kind == TOKEN_RBRACE) {
            return stmt_create_return(value);
        }
        if (starts_with_identifier) {
            fprintf(stderr,
                    "parse error at %zu:%zu: expected '=' for assignment or block end for implicit return\n",
                    parser->current.line,
                    parser->current.column);
            parser->has_error = true;
            return NULL;
        }
        fprintf(stderr,
                "parse error at %zu:%zu: expected ';' or block end after expression\n",
                parser->current.line,
                parser->current.column);
        parser->has_error = true;
        return NULL;
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
            if (parser->current.kind == TOKEN_RPAREN) {
                break;
            }
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
    parser->current = lexer_next(&parser->lexer);
    parser->next = lexer_next(&parser->lexer);
    if (parser->current.kind == TOKEN_ERROR || parser->next.kind == TOKEN_ERROR) {
        parser->has_error = true;
    }
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
