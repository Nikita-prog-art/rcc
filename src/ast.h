#ifndef RCC_AST_H
#define RCC_AST_H

#include "common.h"

typedef enum TypeKind {
    TYPE_I32 = 0
} TypeKind;

typedef enum ExprKind {
    EXPR_INTEGER = 0,
    EXPR_NAME,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL
} ExprKind;

typedef enum UnaryOp {
    UNARY_NEG = 0,
    UNARY_NOT
} UnaryOp;

typedef enum BinaryOp {
    BINARY_ADD = 0,
    BINARY_SUB,
    BINARY_MUL,
    BINARY_REM,
    BINARY_DIV,
    BINARY_EQ,
    BINARY_NE,
    BINARY_LT,
    BINARY_LE,
    BINARY_GT,
    BINARY_GE
} BinaryOp;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Param Param;

struct Param {
    const char *name;
    size_t length;
    TypeKind type;
};

struct Expr {
    ExprKind kind;
    union {
        long integer_value;
        struct {
            const char *name;
            size_t length;
        } name;
        struct {
            UnaryOp op;
            Expr *operand;
        } unary;
        struct {
            BinaryOp op;
            Expr *lhs;
            Expr *rhs;
        } binary;
        struct {
            const char *callee;
            size_t callee_length;
            Expr **args;
            size_t arg_count;
            size_t arg_capacity;
        } call;
    };
};

typedef enum StmtKind {
    STMT_LET = 0,
    STMT_RETURN,
    STMT_IF,
    STMT_ASSIGN,
    STMT_WHILE,
    STMT_BLOCK,
    STMT_BREAK,
    STMT_CONTINUE
} StmtKind;

struct Stmt {
    StmtKind kind;
    union {
        struct {
            const char *name;
            size_t length;
            TypeKind type;
            bool is_mutable;
            Expr *value;
        } let_stmt;
        struct {
            const char *name;
            size_t length;
            Expr *value;
        } assign_stmt;
        struct {
            Expr *value;
        } return_stmt;
        struct {
            Expr *condition;
            Stmt **then_statements;
            size_t then_count;
            size_t then_capacity;
            Stmt **else_statements;
            size_t else_count;
            size_t else_capacity;
        } if_stmt;
        struct {
            Expr *condition;
            Stmt **body_statements;
            size_t body_count;
            size_t body_capacity;
        } while_stmt;
        struct {
            Stmt **statements;
            size_t statement_count;
            size_t statement_capacity;
        } block_stmt;
    };
};

typedef struct Function {
    const char *name;
    size_t name_length;
    TypeKind return_type;
    Param *params;
    size_t param_count;
    size_t param_capacity;
    Stmt **statements;
    size_t statement_count;
    size_t statement_capacity;
} Function;

typedef struct Program {
    Function **functions;
    size_t function_count;
    size_t function_capacity;
} Program;

Program *program_create(void);
void program_destroy(Program *program);
Function *function_create(const char *name, size_t name_length, TypeKind return_type);
void function_append_param(Function *function, const char *name, size_t length, TypeKind type);
void function_append_statement(Function *function, Stmt *statement);
Expr *expr_create_integer(long value);
Expr *expr_create_name(const char *name, size_t length);
Expr *expr_create_unary(UnaryOp op, Expr *operand);
Expr *expr_create_binary(BinaryOp op, Expr *lhs, Expr *rhs);
Expr *expr_create_call(const char *callee, size_t callee_length);
void expr_append_call_arg(Expr *expr, Expr *arg);
Stmt *stmt_create_let(const char *name, size_t length, TypeKind type, bool is_mutable, Expr *value);
Stmt *stmt_create_assign(const char *name, size_t length, Expr *value);
Stmt *stmt_create_return(Expr *value);
Stmt *stmt_create_if(Expr *condition);
Stmt *stmt_create_while(Expr *condition);
Stmt *stmt_create_block(void);
Stmt *stmt_create_break(void);
Stmt *stmt_create_continue(void);
void stmt_append_then_statement(Stmt *stmt, Stmt *child);
void stmt_append_else_statement(Stmt *stmt, Stmt *child);
void stmt_append_while_statement(Stmt *stmt, Stmt *child);
void stmt_append_block_statement(Stmt *stmt, Stmt *child);
void program_append_function(Program *program, Function *function);

#endif
