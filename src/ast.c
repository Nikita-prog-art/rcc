#include "ast.h"

#include <stdlib.h>
#include <string.h>

static void *xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (ptr == NULL) {
        abort();
    }
    return ptr;
}

static void *xrealloc(void *ptr, size_t size) {
    void *next = realloc(ptr, size);
    if (next == NULL) {
        abort();
    }
    return next;
}

static void grow_ptr_array(void ***items, size_t *capacity, size_t item_count) {
    if (item_count < *capacity) {
        return;
    }
    size_t next_capacity = *capacity == 0 ? 8 : *capacity * 2;
    *items = xrealloc(*items, next_capacity * sizeof(void *));
    *capacity = next_capacity;
}

static void destroy_expr(Expr *expr) {
    if (expr == NULL) {
        return;
    }
    if (expr->kind == EXPR_BINARY) {
        destroy_expr(expr->binary.lhs);
        destroy_expr(expr->binary.rhs);
    }
    free(expr);
}

static void destroy_stmt(Stmt *stmt) {
    if (stmt == NULL) {
        return;
    }
    switch (stmt->kind) {
        case STMT_LET:
            destroy_expr(stmt->let_stmt.value);
            break;
        case STMT_RETURN:
            destroy_expr(stmt->return_stmt.value);
            break;
    }
    free(stmt);
}

Program *program_create(void) {
    return xcalloc(1, sizeof(Program));
}

void program_destroy(Program *program) {
    if (program == NULL) {
        return;
    }
    for (size_t i = 0; i < program->function_count; i++) {
        Function *function = program->functions[i];
        if (function != NULL) {
            for (size_t j = 0; j < function->statement_count; j++) {
                destroy_stmt(function->statements[j]);
            }
            free(function->statements);
            free(function);
        }
    }
    free(program->functions);
    free(program);
}

Function *function_create(const char *name, size_t name_length, TypeKind return_type) {
    Function *function = xcalloc(1, sizeof(Function));
    function->name = name;
    function->name_length = name_length;
    function->return_type = return_type;
    return function;
}

void function_append_statement(Function *function, Stmt *statement) {
    grow_ptr_array((void ***) &function->statements, &function->statement_capacity, function->statement_count);
    function->statements[function->statement_count++] = statement;
}

Expr *expr_create_integer(long value) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_INTEGER;
    expr->integer_value = value;
    return expr;
}

Expr *expr_create_name(const char *name, size_t length) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_NAME;
    expr->name.name = name;
    expr->name.length = length;
    return expr;
}

Expr *expr_create_binary(BinaryOp op, Expr *lhs, Expr *rhs) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_BINARY;
    expr->binary.op = op;
    expr->binary.lhs = lhs;
    expr->binary.rhs = rhs;
    return expr;
}

Stmt *stmt_create_let(const char *name, size_t length, TypeKind type, Expr *value) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_LET;
    stmt->let_stmt.name = name;
    stmt->let_stmt.length = length;
    stmt->let_stmt.type = type;
    stmt->let_stmt.value = value;
    return stmt;
}

Stmt *stmt_create_return(Expr *value) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_RETURN;
    stmt->return_stmt.value = value;
    return stmt;
}

void program_append_function(Program *program, Function *function) {
    grow_ptr_array((void ***) &program->functions, &program->function_capacity, program->function_count);
    program->functions[program->function_count++] = function;
}
