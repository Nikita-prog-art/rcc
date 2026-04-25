#ifndef RCC_AST_H
#define RCC_AST_H

#include "common.h"

typedef enum TypeKind {
    TYPE_I32 = 0
} TypeKind;

typedef enum ExprKind {
    EXPR_INTEGER = 0,
    EXPR_NAME,
    EXPR_BINARY
} ExprKind;

typedef enum BinaryOp {
    BINARY_ADD = 0,
    BINARY_SUB,
    BINARY_MUL,
    BINARY_DIV
} BinaryOp;

typedef struct Expr Expr;
typedef struct Stmt Stmt;

struct Expr {
    ExprKind kind;
    union {
        long integer_value;
        struct {
            const char *name;
            size_t length;
        } name;
        struct {
            BinaryOp op;
            Expr *lhs;
            Expr *rhs;
        } binary;
    };
};

typedef enum StmtKind {
    STMT_LET = 0,
    STMT_RETURN
} StmtKind;

struct Stmt {
    StmtKind kind;
    union {
        struct {
            const char *name;
            size_t length;
            TypeKind type;
            Expr *value;
        } let_stmt;
        struct {
            Expr *value;
        } return_stmt;
    };
};

typedef struct Function {
    const char *name;
    size_t name_length;
    TypeKind return_type;
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
void function_append_statement(Function *function, Stmt *statement);
Expr *expr_create_integer(long value);
Expr *expr_create_name(const char *name, size_t length);
Expr *expr_create_binary(BinaryOp op, Expr *lhs, Expr *rhs);
Stmt *stmt_create_let(const char *name, size_t length, TypeKind type, Expr *value);
Stmt *stmt_create_return(Expr *value);
void program_append_function(Program *program, Function *function);

#endif
