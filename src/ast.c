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

static char *copy_string(const char *value, size_t length) {
    char *buffer = xcalloc(length + 1, 1);
    memcpy(buffer, value, length);
    return buffer;
}

static void destroy_stmt_impl(Stmt *stmt);

static void destroy_block(Block *block) {
    for (size_t i = 0; i < block->count; i++) {
        destroy_stmt_impl(block->statements[i]);
    }
    free(block->statements);
}

static void destroy_expr_impl(Expr *expr) {
    if (expr == NULL) {
        return;
    }
    if (expr->kind == EXPR_BINARY) {
        destroy_expr_impl(expr->binary.lhs);
        destroy_expr_impl(expr->binary.rhs);
    } else if (expr->kind == EXPR_UNARY) {
        destroy_expr_impl(expr->unary.operand);
    } else if (expr->kind == EXPR_CALL) {
        for (size_t i = 0; i < expr->call.arg_count; i++) {
            destroy_expr_impl(expr->call.args[i]);
        }
        free(expr->call.callee);
        free(expr->call.args);
    } else if (expr->kind == EXPR_NAME) {
        free(expr->name.name);
    }
    free(expr);
}

static void destroy_stmt_impl(Stmt *stmt) {
    if (stmt == NULL) {
        return;
    }
    switch (stmt->kind) {
        case STMT_LET:
            destroy_expr_impl(stmt->let_stmt.value);
            free(stmt->let_stmt.name);
            break;
        case STMT_EXPR:
            destroy_expr_impl(stmt->expr_stmt.value);
            break;
        case STMT_ASSIGN:
            destroy_expr_impl(stmt->assign_stmt.value);
            free(stmt->assign_stmt.name);
            break;
        case STMT_RETURN:
            destroy_expr_impl(stmt->return_stmt.value);
            break;
        case STMT_IF:
            destroy_expr_impl(stmt->if_stmt.condition);
            destroy_block(&stmt->if_stmt.then_block);
            destroy_block(&stmt->if_stmt.else_block);
            break;
        case STMT_WHILE:
            destroy_expr_impl(stmt->while_stmt.condition);
            destroy_block(&stmt->while_stmt.body);
            break;
        case STMT_LOOP:
            destroy_block(&stmt->loop_stmt.body);
            break;
        case STMT_BLOCK:
            destroy_block(&stmt->block_stmt.body);
            break;
        case STMT_EMPTY:
        case STMT_BREAK:
        case STMT_CONTINUE:
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
            for (size_t j = 0; j < function->param_count; j++) {
                free(function->params[j].name);
            }
            destroy_block(&function->body);
            free(function->name);
            free(function->params);
            free(function);
        }
    }
    free(program->functions);
    free(program);
}

void stmt_destroy(Stmt *stmt) {
    destroy_stmt_impl(stmt);
}

void expr_destroy(Expr *expr) {
    destroy_expr_impl(expr);
}

Function *function_create(const char *name, size_t name_length, TypeKind return_type, SourceSpan span) {
    Function *function = xcalloc(1, sizeof(Function));
    function->name = copy_string(name, name_length);
    function->name_length = name_length;
    function->span = span;
    function->return_type = return_type;
    return function;
}

void function_append_param(Function *function, const char *name, size_t length, TypeKind type, SourceSpan span) {
    if (function->param_count >= function->param_capacity) {
        size_t next_capacity = function->param_capacity == 0 ? 4 : function->param_capacity * 2;
        function->params = xrealloc(function->params, next_capacity * sizeof(Param));
        function->param_capacity = next_capacity;
    }
    function->params[function->param_count++] = (Param) {
        .name = copy_string(name, length),
        .length = length,
        .span = span,
        .type = type
    };
}

void function_append_statement(Function *function, Stmt *statement) {
    block_append_statement(&function->body, statement);
}

Expr *expr_create_integer(long value, SourceSpan span) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_INTEGER;
    expr->span = span;
    expr->integer_value = value;
    return expr;
}

Expr *expr_create_name(const char *name, size_t length, SourceSpan span) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_NAME;
    expr->span = span;
    expr->name.name = copy_string(name, length);
    expr->name.length = length;
    return expr;
}

Expr *expr_create_unary(UnaryOp op, Expr *operand, SourceSpan span) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_UNARY;
    expr->span = span;
    expr->unary.op = op;
    expr->unary.operand = operand;
    return expr;
}

Expr *expr_create_binary(BinaryOp op, Expr *lhs, Expr *rhs, SourceSpan span) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_BINARY;
    expr->span = span;
    expr->binary.op = op;
    expr->binary.lhs = lhs;
    expr->binary.rhs = rhs;
    return expr;
}

Expr *expr_create_call(const char *callee, size_t callee_length, SourceSpan span) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_CALL;
    expr->span = span;
    expr->call.callee = copy_string(callee, callee_length);
    expr->call.callee_length = callee_length;
    return expr;
}

void expr_append_call_arg(Expr *expr, Expr *arg) {
    if (expr->call.arg_count >= expr->call.arg_capacity) {
        size_t next_capacity = expr->call.arg_capacity == 0 ? 4 : expr->call.arg_capacity * 2;
        expr->call.args = xrealloc(expr->call.args, next_capacity * sizeof(Expr *));
        expr->call.arg_capacity = next_capacity;
    }
    expr->call.args[expr->call.arg_count++] = arg;
}

Stmt *stmt_create_let(const char *name, size_t length, TypeKind type, bool is_mutable, Expr *value, SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_LET;
    stmt->span = span;
    stmt->let_stmt.name = copy_string(name, length);
    stmt->let_stmt.length = length;
    stmt->let_stmt.type = type;
    stmt->let_stmt.is_mutable = is_mutable;
    stmt->let_stmt.value = value;
    return stmt;
}

Stmt *stmt_create_assign(const char *name, size_t length, Expr *value, SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_ASSIGN;
    stmt->span = span;
    stmt->assign_stmt.name = copy_string(name, length);
    stmt->assign_stmt.length = length;
    stmt->assign_stmt.value = value;
    return stmt;
}

Stmt *stmt_create_expr(Expr *value, SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_EXPR;
    stmt->span = span;
    stmt->expr_stmt.value = value;
    return stmt;
}

Stmt *stmt_create_return(Expr *value, SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_RETURN;
    stmt->span = span;
    stmt->return_stmt.value = value;
    return stmt;
}

Stmt *stmt_create_if(Expr *condition, SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_IF;
    stmt->span = span;
    stmt->if_stmt.condition = condition;
    return stmt;
}

Stmt *stmt_create_while(Expr *condition, SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_WHILE;
    stmt->span = span;
    stmt->while_stmt.condition = condition;
    return stmt;
}

Stmt *stmt_create_loop(SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_LOOP;
    stmt->span = span;
    return stmt;
}

Stmt *stmt_create_block(SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_BLOCK;
    stmt->span = span;
    return stmt;
}

Stmt *stmt_create_empty(SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_EMPTY;
    stmt->span = span;
    return stmt;
}

Stmt *stmt_create_break(SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_BREAK;
    stmt->span = span;
    return stmt;
}

Stmt *stmt_create_continue(SourceSpan span) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_CONTINUE;
    stmt->span = span;
    return stmt;
}

void block_append_statement(Block *block, Stmt *child) {
    if (block->count >= block->capacity) {
        size_t next_capacity = block->capacity == 0 ? 8 : block->capacity * 2;
        block->statements = xrealloc(block->statements, next_capacity * sizeof(Stmt *));
        block->capacity = next_capacity;
    }
    block->statements[block->count++] = child;
}

void stmt_append_then_statement(Stmt *stmt, Stmt *child) {
    block_append_statement(&stmt->if_stmt.then_block, child);
}

void stmt_append_else_statement(Stmt *stmt, Stmt *child) {
    block_append_statement(&stmt->if_stmt.else_block, child);
}

void stmt_append_while_statement(Stmt *stmt, Stmt *child) {
    block_append_statement(&stmt->while_stmt.body, child);
}

void stmt_append_loop_statement(Stmt *stmt, Stmt *child) {
    block_append_statement(&stmt->loop_stmt.body, child);
}

void stmt_append_block_statement(Stmt *stmt, Stmt *child) {
    block_append_statement(&stmt->block_stmt.body, child);
}

void program_append_function(Program *program, Function *function) {
    if (program->function_count >= program->function_capacity) {
        size_t next_capacity = program->function_capacity == 0 ? 8 : program->function_capacity * 2;
        program->functions = xrealloc(program->functions, next_capacity * sizeof(Function *));
        program->function_capacity = next_capacity;
    }
    program->functions[program->function_count++] = function;
}
