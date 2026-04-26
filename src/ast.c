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
        free(expr->call.args);
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
            break;
        case STMT_ASSIGN:
            destroy_expr_impl(stmt->assign_stmt.value);
            break;
        case STMT_RETURN:
            destroy_expr_impl(stmt->return_stmt.value);
            break;
        case STMT_IF:
            destroy_expr_impl(stmt->if_stmt.condition);
            for (size_t i = 0; i < stmt->if_stmt.then_count; i++) {
                destroy_stmt_impl(stmt->if_stmt.then_statements[i]);
            }
            for (size_t i = 0; i < stmt->if_stmt.else_count; i++) {
                destroy_stmt_impl(stmt->if_stmt.else_statements[i]);
            }
            free(stmt->if_stmt.then_statements);
            free(stmt->if_stmt.else_statements);
            break;
        case STMT_WHILE:
            destroy_expr_impl(stmt->while_stmt.condition);
            for (size_t i = 0; i < stmt->while_stmt.body_count; i++) {
                destroy_stmt_impl(stmt->while_stmt.body_statements[i]);
            }
            free(stmt->while_stmt.body_statements);
            break;
        case STMT_LOOP:
            for (size_t i = 0; i < stmt->loop_stmt.body_count; i++) {
                destroy_stmt_impl(stmt->loop_stmt.body_statements[i]);
            }
            free(stmt->loop_stmt.body_statements);
            break;
        case STMT_BLOCK:
            for (size_t i = 0; i < stmt->block_stmt.statement_count; i++) {
                destroy_stmt_impl(stmt->block_stmt.statements[i]);
            }
            free(stmt->block_stmt.statements);
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
            for (size_t j = 0; j < function->statement_count; j++) {
                destroy_stmt_impl(function->statements[j]);
            }
            free(function->params);
            free(function->statements);
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

Function *function_create(const char *name, size_t name_length, TypeKind return_type) {
    Function *function = xcalloc(1, sizeof(Function));
    function->name = name;
    function->name_length = name_length;
    function->return_type = return_type;
    return function;
}

void function_append_param(Function *function, const char *name, size_t length, TypeKind type) {
    if (function->param_count >= function->param_capacity) {
        size_t next_capacity = function->param_capacity == 0 ? 4 : function->param_capacity * 2;
        function->params = xrealloc(function->params, next_capacity * sizeof(Param));
        function->param_capacity = next_capacity;
    }
    function->params[function->param_count++] = (Param) {
        .name = name,
        .length = length,
        .type = type
    };
}

void function_append_statement(Function *function, Stmt *statement) {
    if (function->statement_count >= function->statement_capacity) {
        size_t next_capacity = function->statement_capacity == 0 ? 8 : function->statement_capacity * 2;
        function->statements = xrealloc(function->statements, next_capacity * sizeof(Stmt *));
        function->statement_capacity = next_capacity;
    }
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

Expr *expr_create_unary(UnaryOp op, Expr *operand) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_UNARY;
    expr->unary.op = op;
    expr->unary.operand = operand;
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

Expr *expr_create_call(const char *callee, size_t callee_length) {
    Expr *expr = xcalloc(1, sizeof(Expr));
    expr->kind = EXPR_CALL;
    expr->call.callee = callee;
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

Stmt *stmt_create_let(const char *name, size_t length, TypeKind type, bool is_mutable, Expr *value) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_LET;
    stmt->let_stmt.name = name;
    stmt->let_stmt.length = length;
    stmt->let_stmt.type = type;
    stmt->let_stmt.is_mutable = is_mutable;
    stmt->let_stmt.value = value;
    return stmt;
}

Stmt *stmt_create_assign(const char *name, size_t length, Expr *value) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_ASSIGN;
    stmt->assign_stmt.name = name;
    stmt->assign_stmt.length = length;
    stmt->assign_stmt.value = value;
    return stmt;
}

Stmt *stmt_create_return(Expr *value) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_RETURN;
    stmt->return_stmt.value = value;
    return stmt;
}

Stmt *stmt_create_if(Expr *condition) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_IF;
    stmt->if_stmt.condition = condition;
    return stmt;
}

Stmt *stmt_create_while(Expr *condition) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_WHILE;
    stmt->while_stmt.condition = condition;
    return stmt;
}

Stmt *stmt_create_loop(void) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_LOOP;
    return stmt;
}

Stmt *stmt_create_block(void) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_BLOCK;
    return stmt;
}

Stmt *stmt_create_empty(void) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_EMPTY;
    return stmt;
}

Stmt *stmt_create_break(void) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_BREAK;
    return stmt;
}

Stmt *stmt_create_continue(void) {
    Stmt *stmt = xcalloc(1, sizeof(Stmt));
    stmt->kind = STMT_CONTINUE;
    return stmt;
}

void stmt_append_then_statement(Stmt *stmt, Stmt *child) {
    if (stmt->if_stmt.then_count >= stmt->if_stmt.then_capacity) {
        size_t next_capacity = stmt->if_stmt.then_capacity == 0 ? 8 : stmt->if_stmt.then_capacity * 2;
        stmt->if_stmt.then_statements = xrealloc(stmt->if_stmt.then_statements, next_capacity * sizeof(Stmt *));
        stmt->if_stmt.then_capacity = next_capacity;
    }
    stmt->if_stmt.then_statements[stmt->if_stmt.then_count++] = child;
}

void stmt_append_else_statement(Stmt *stmt, Stmt *child) {
    if (stmt->if_stmt.else_count >= stmt->if_stmt.else_capacity) {
        size_t next_capacity = stmt->if_stmt.else_capacity == 0 ? 8 : stmt->if_stmt.else_capacity * 2;
        stmt->if_stmt.else_statements = xrealloc(stmt->if_stmt.else_statements, next_capacity * sizeof(Stmt *));
        stmt->if_stmt.else_capacity = next_capacity;
    }
    stmt->if_stmt.else_statements[stmt->if_stmt.else_count++] = child;
}

void stmt_append_while_statement(Stmt *stmt, Stmt *child) {
    if (stmt->while_stmt.body_count >= stmt->while_stmt.body_capacity) {
        size_t next_capacity = stmt->while_stmt.body_capacity == 0 ? 8 : stmt->while_stmt.body_capacity * 2;
        stmt->while_stmt.body_statements = xrealloc(stmt->while_stmt.body_statements, next_capacity * sizeof(Stmt *));
        stmt->while_stmt.body_capacity = next_capacity;
    }
    stmt->while_stmt.body_statements[stmt->while_stmt.body_count++] = child;
}

void stmt_append_loop_statement(Stmt *stmt, Stmt *child) {
    if (stmt->loop_stmt.body_count >= stmt->loop_stmt.body_capacity) {
        size_t next_capacity = stmt->loop_stmt.body_capacity == 0 ? 8 : stmt->loop_stmt.body_capacity * 2;
        stmt->loop_stmt.body_statements = xrealloc(stmt->loop_stmt.body_statements, next_capacity * sizeof(Stmt *));
        stmt->loop_stmt.body_capacity = next_capacity;
    }
    stmt->loop_stmt.body_statements[stmt->loop_stmt.body_count++] = child;
}

void stmt_append_block_statement(Stmt *stmt, Stmt *child) {
    if (stmt->block_stmt.statement_count >= stmt->block_stmt.statement_capacity) {
        size_t next_capacity = stmt->block_stmt.statement_capacity == 0 ? 8 : stmt->block_stmt.statement_capacity * 2;
        stmt->block_stmt.statements = xrealloc(stmt->block_stmt.statements, next_capacity * sizeof(Stmt *));
        stmt->block_stmt.statement_capacity = next_capacity;
    }
    stmt->block_stmt.statements[stmt->block_stmt.statement_count++] = child;
}

void program_append_function(Program *program, Function *function) {
    if (program->function_count >= program->function_capacity) {
        size_t next_capacity = program->function_capacity == 0 ? 8 : program->function_capacity * 2;
        program->functions = xrealloc(program->functions, next_capacity * sizeof(Function *));
        program->function_capacity = next_capacity;
    }
    program->functions[program->function_count++] = function;
}
