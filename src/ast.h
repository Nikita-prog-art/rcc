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

typedef enum FlowFlags {
    FLOW_NONE = 0,
    FLOW_FALLTHROUGH = 1 << 0,
    FLOW_RETURN = 1 << 1,
    FLOW_BREAK = 1 << 2,
    FLOW_CONTINUE = 1 << 3,
    FLOW_DIVERGE = 1 << 4
} FlowFlags;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Param Param;

typedef struct Block {
    Stmt **statements;
    size_t count;
    size_t capacity;
} Block;

struct Param {
    char *name;
    size_t length;
    SourceSpan span;
    TypeKind type;
    size_t symbol_id;
};

struct Expr {
    ExprKind kind;
    SourceSpan span;
    TypeKind type;
    bool has_type;
    union {
        long integer_value;
        struct {
            char *name;
            size_t length;
            size_t symbol_id;
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
            char *callee;
            size_t callee_length;
            size_t function_id;
            Expr **args;
            size_t arg_count;
            size_t arg_capacity;
        } call;
    };
};

typedef enum StmtKind {
    STMT_LET = 0,
    STMT_EXPR,
    STMT_RETURN,
    STMT_IF,
    STMT_ASSIGN,
    STMT_WHILE,
    STMT_LOOP,
    STMT_BLOCK,
    STMT_EMPTY,
    STMT_BREAK,
    STMT_CONTINUE
} StmtKind;

struct Stmt {
    StmtKind kind;
    SourceSpan span;
    FlowFlags flow_flags;
    union {
        struct {
            char *name;
            size_t length;
            TypeKind type;
            bool is_mutable;
            size_t symbol_id;
            Expr *value;
        } let_stmt;
        struct {
            char *name;
            size_t length;
            size_t symbol_id;
            Expr *value;
        } assign_stmt;
        struct {
            Expr *value;
        } expr_stmt;
        struct {
            Expr *value;
        } return_stmt;
        struct {
            Expr *condition;
            Block then_block;
            bool has_else;
            Block else_block;
        } if_stmt;
        struct {
            Expr *condition;
            Block body;
        } while_stmt;
        struct {
            Block body;
        } loop_stmt;
        struct {
            Block body;
        } block_stmt;
    };
};

typedef struct Function {
    char *name;
    size_t name_length;
    SourceSpan span;
    size_t function_id;
    TypeKind return_type;
    Param *params;
    size_t param_count;
    size_t param_capacity;
    size_t symbol_count;
    Block body;
} Function;

typedef struct Program {
    Function **functions;
    size_t function_count;
    size_t function_capacity;
} Program;

Program *program_create(void);
void program_destroy(Program *program);
void stmt_destroy(Stmt *stmt);
void expr_destroy(Expr *expr);
Function *function_create(const char *name, size_t name_length, TypeKind return_type, SourceSpan span);
void function_append_param(Function *function, const char *name, size_t length, TypeKind type, SourceSpan span);
void function_append_statement(Function *function, Stmt *statement);
Expr *expr_create_integer(long value, SourceSpan span);
Expr *expr_create_name(const char *name, size_t length, SourceSpan span);
Expr *expr_create_unary(UnaryOp op, Expr *operand, SourceSpan span);
Expr *expr_create_binary(BinaryOp op, Expr *lhs, Expr *rhs, SourceSpan span);
Expr *expr_create_call(const char *callee, size_t callee_length, SourceSpan span);
void expr_append_call_arg(Expr *expr, Expr *arg);
Stmt *stmt_create_let(const char *name, size_t length, TypeKind type, bool is_mutable, Expr *value, SourceSpan span);
Stmt *stmt_create_assign(const char *name, size_t length, Expr *value, SourceSpan span);
Stmt *stmt_create_expr(Expr *value, SourceSpan span);
Stmt *stmt_create_return(Expr *value, SourceSpan span);
Stmt *stmt_create_if(Expr *condition, SourceSpan span);
Stmt *stmt_create_while(Expr *condition, SourceSpan span);
Stmt *stmt_create_loop(SourceSpan span);
Stmt *stmt_create_block(SourceSpan span);
Stmt *stmt_create_empty(SourceSpan span);
Stmt *stmt_create_break(SourceSpan span);
Stmt *stmt_create_continue(SourceSpan span);
void block_append_statement(Block *block, Stmt *child);
void stmt_append_then_statement(Stmt *stmt, Stmt *child);
void stmt_append_else_statement(Stmt *stmt, Stmt *child);
void stmt_append_while_statement(Stmt *stmt, Stmt *child);
void stmt_append_loop_statement(Stmt *stmt, Stmt *child);
void stmt_append_block_statement(Stmt *stmt, Stmt *child);
void program_append_function(Program *program, Function *function);

#endif
