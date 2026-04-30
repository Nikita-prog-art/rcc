#include "semantic.h"

#include <stdlib.h>
#include <string.h>

typedef struct Symbol {
    const char *name;
    size_t length;
    TypeKind type;
    bool is_mutable;
    size_t symbol_id;
} Symbol;

typedef struct SymbolTable {
    Symbol *items;
    size_t count;
    size_t capacity;
    size_t next_symbol_id;
} SymbolTable;

static bool same_name(const char *lhs, size_t lhs_len, const char *rhs, size_t rhs_len) {
    return lhs_len == rhs_len && strncmp(lhs, rhs, lhs_len) == 0;
}

static void *xrealloc(void *ptr, size_t size) {
    void *next = realloc(ptr, size);
    if (next == NULL) {
        abort();
    }
    return next;
}

static const CheckedFunction *lookup_function(const CheckedProgram *checked,
                                              const char *name,
                                              size_t length,
                                              size_t *out_id) {
    for (size_t i = 0; i < checked->function_count; i++) {
        const Function *function = checked->functions[i].function;
        if (same_name(name, length, function->name, function->name_length)) {
            if (out_id != NULL) {
                *out_id = i;
            }
            return &checked->functions[i];
        }
    }
    return NULL;
}

static const Symbol *lookup_symbol(const SymbolTable *symbols, const char *name, size_t length) {
    for (size_t i = symbols->count; i > 0; i--) {
        if (same_name(name, length, symbols->items[i - 1].name, symbols->items[i - 1].length)) {
            return &symbols->items[i - 1];
        }
    }
    return NULL;
}

static bool symbol_table_insert(SymbolTable *symbols,
                                size_t scope_start,
                                const char *name,
                                size_t length,
                                TypeKind type,
                                bool is_mutable,
                                SourceSpan span,
                                DiagnosticSink *diagnostics,
                                size_t *out_symbol_id) {
    for (size_t i = scope_start; i < symbols->count; i++) {
        if (same_name(name, length, symbols->items[i].name, symbols->items[i].length)) {
            diagnostic_error(diagnostics, span, "semantic", "duplicate variable '%.*s'", (int) length, name);
            return false;
        }
    }
    if (symbols->count >= symbols->capacity) {
        size_t next_capacity = symbols->capacity == 0 ? 32 : symbols->capacity * 2;
        symbols->items = xrealloc(symbols->items, next_capacity * sizeof(Symbol));
        symbols->capacity = next_capacity;
    }

    size_t symbol_id = symbols->next_symbol_id++;
    symbols->items[symbols->count++] = (Symbol) {
        .name = name,
        .length = length,
        .type = type,
        .is_mutable = is_mutable,
        .symbol_id = symbol_id
    };
    if (out_symbol_id != NULL) {
        *out_symbol_id = symbol_id;
    }
    return true;
}

static bool checked_program_append(CheckedProgram *checked, Function *function) {
    if (checked->function_count >= checked->function_capacity) {
        size_t next_capacity = checked->function_capacity == 0 ? 16 : checked->function_capacity * 2;
        checked->functions = xrealloc(checked->functions, next_capacity * sizeof(CheckedFunction));
        checked->function_capacity = next_capacity;
    }
    function->function_id = checked->function_count;
    checked->functions[checked->function_count++] = (CheckedFunction) {
        .function = function,
        .param_count = function->param_count,
        .return_type = function->return_type
    };
    return true;
}

static bool check_expr(Expr *expr,
                       SymbolTable *symbols,
                       const CheckedProgram *checked,
                       DiagnosticSink *diagnostics) {
    expr->type = TYPE_I32;
    expr->has_type = true;

    switch (expr->kind) {
        case EXPR_INTEGER:
            return true;
        case EXPR_NAME: {
            const Symbol *symbol = lookup_symbol(symbols, expr->name.name, expr->name.length);
            if (symbol == NULL) {
                diagnostic_error(diagnostics,
                                 expr->span,
                                 "semantic",
                                 "use of undefined variable '%.*s'",
                                 (int) expr->name.length,
                                 expr->name.name);
                return false;
            }
            expr->name.symbol_id = symbol->symbol_id;
            expr->type = symbol->type;
            return true;
        }
        case EXPR_UNARY:
            return check_expr(expr->unary.operand, symbols, checked, diagnostics);
        case EXPR_BINARY:
            if (!check_expr(expr->binary.lhs, symbols, checked, diagnostics) ||
                !check_expr(expr->binary.rhs, symbols, checked, diagnostics)) {
                return false;
            }
            if ((expr->binary.op == BINARY_DIV || expr->binary.op == BINARY_REM) &&
                expr->binary.rhs->kind == EXPR_INTEGER &&
                expr->binary.rhs->integer_value == 0) {
                diagnostic_error(diagnostics, expr->binary.rhs->span, "semantic", "division by zero");
                return false;
            }
            return true;
        case EXPR_CALL: {
            size_t function_id = 0;
            const CheckedFunction *signature =
                lookup_function(checked, expr->call.callee, expr->call.callee_length, &function_id);
            if (signature == NULL) {
                diagnostic_error(diagnostics,
                                 expr->span,
                                 "semantic",
                                 "call to undefined function '%.*s'",
                                 (int) expr->call.callee_length,
                                 expr->call.callee);
                return false;
            }
            expr->call.function_id = function_id;
            if (signature->param_count != expr->call.arg_count) {
                diagnostic_error(diagnostics,
                                 expr->span,
                                 "semantic",
                                 "function '%.*s' expects %zu args, got %zu",
                                 (int) expr->call.callee_length,
                                 expr->call.callee,
                                 signature->param_count,
                                 expr->call.arg_count);
                return false;
            }
            for (size_t i = 0; i < expr->call.arg_count; i++) {
                if (!check_expr(expr->call.args[i], symbols, checked, diagnostics)) {
                    return false;
                }
            }
            expr->type = signature->return_type;
            return true;
        }
    }
    return false;
}

static bool check_statements(const Block *block,
                             SymbolTable *symbols,
                             size_t scope_start,
                             const CheckedProgram *checked,
                             size_t loop_depth,
                             DiagnosticSink *diagnostics,
                             FlowFlags *out_flow);

static bool check_scoped_block(const Block *block,
                               SymbolTable *symbols,
                               const CheckedProgram *checked,
                               size_t loop_depth,
                               DiagnosticSink *diagnostics,
                               FlowFlags *out_flow) {
    size_t saved_count = symbols->count;
    bool ok = check_statements(block, symbols, saved_count, checked, loop_depth, diagnostics, out_flow);
    symbols->count = saved_count;
    return ok;
}

static bool check_statement(Stmt *stmt,
                            SymbolTable *symbols,
                            size_t scope_start,
                            const CheckedProgram *checked,
                            size_t loop_depth,
                            DiagnosticSink *diagnostics,
                            FlowFlags *out_flow) {
    if (stmt->kind == STMT_LET) {
        if (!check_expr(stmt->let_stmt.value, symbols, checked, diagnostics)) {
            return false;
        }
        if (!symbol_table_insert(symbols,
                                 scope_start,
                                 stmt->let_stmt.name,
                                 stmt->let_stmt.length,
                                 stmt->let_stmt.type,
                                 stmt->let_stmt.is_mutable,
                                 stmt->span,
                                 diagnostics,
                                 &stmt->let_stmt.symbol_id)) {
            return false;
        }
        *out_flow = FLOW_FALLTHROUGH;
        return true;
    }

    if (stmt->kind == STMT_ASSIGN) {
        const Symbol *symbol = lookup_symbol(symbols, stmt->assign_stmt.name, stmt->assign_stmt.length);
        if (symbol == NULL) {
            diagnostic_error(diagnostics,
                             stmt->span,
                             "semantic",
                             "assignment to undefined variable '%.*s'",
                             (int) stmt->assign_stmt.length,
                             stmt->assign_stmt.name);
            return false;
        }
        if (!symbol->is_mutable) {
            diagnostic_error(diagnostics,
                             stmt->span,
                             "semantic",
                             "variable '%.*s' is not mutable",
                             (int) stmt->assign_stmt.length,
                             stmt->assign_stmt.name);
            return false;
        }
        stmt->assign_stmt.symbol_id = symbol->symbol_id;
        if (!check_expr(stmt->assign_stmt.value, symbols, checked, diagnostics)) {
            return false;
        }
        *out_flow = FLOW_FALLTHROUGH;
        return true;
    }

    if (stmt->kind == STMT_EXPR) {
        if (!check_expr(stmt->expr_stmt.value, symbols, checked, diagnostics)) {
            return false;
        }
        *out_flow = FLOW_FALLTHROUGH;
        return true;
    }

    if (stmt->kind == STMT_RETURN) {
        if (!check_expr(stmt->return_stmt.value, symbols, checked, diagnostics)) {
            return false;
        }
        *out_flow = FLOW_RETURN;
        return true;
    }

    if (stmt->kind == STMT_IF) {
        FlowFlags then_flow = FLOW_NONE;
        FlowFlags else_flow = FLOW_FALLTHROUGH;

        if (!check_expr(stmt->if_stmt.condition, symbols, checked, diagnostics)) {
            return false;
        }
        if (!check_scoped_block(&stmt->if_stmt.then_block, symbols, checked, loop_depth, diagnostics, &then_flow)) {
            return false;
        }
        if (stmt->if_stmt.has_else &&
            !check_scoped_block(&stmt->if_stmt.else_block, symbols, checked, loop_depth, diagnostics, &else_flow)) {
            return false;
        }
        *out_flow = then_flow | else_flow;
        return true;
    }

    if (stmt->kind == STMT_WHILE) {
        FlowFlags body_flow = FLOW_NONE;
        if (!check_expr(stmt->while_stmt.condition, symbols, checked, diagnostics)) {
            return false;
        }
        if (!check_scoped_block(&stmt->while_stmt.body, symbols, checked, loop_depth + 1, diagnostics, &body_flow)) {
            return false;
        }
        *out_flow = FLOW_FALLTHROUGH | (body_flow & FLOW_RETURN);
        return true;
    }

    if (stmt->kind == STMT_LOOP) {
        FlowFlags body_flow = FLOW_NONE;
        FlowFlags loop_flow = FLOW_NONE;
        if (!check_scoped_block(&stmt->loop_stmt.body, symbols, checked, loop_depth + 1, diagnostics, &body_flow)) {
            return false;
        }
        if ((body_flow & FLOW_RETURN) != 0) {
            loop_flow |= FLOW_RETURN;
        }
        if ((body_flow & FLOW_BREAK) != 0) {
            loop_flow |= FLOW_FALLTHROUGH;
        }
        if ((body_flow & (FLOW_FALLTHROUGH | FLOW_CONTINUE | FLOW_DIVERGE)) != 0) {
            loop_flow |= FLOW_DIVERGE;
        }
        *out_flow = loop_flow == FLOW_NONE ? FLOW_DIVERGE : loop_flow;
        return true;
    }

    if (stmt->kind == STMT_BLOCK) {
        return check_scoped_block(&stmt->block_stmt.body, symbols, checked, loop_depth, diagnostics, out_flow);
    }

    if (stmt->kind == STMT_EMPTY) {
        *out_flow = FLOW_FALLTHROUGH;
        return true;
    }

    if (stmt->kind == STMT_BREAK || stmt->kind == STMT_CONTINUE) {
        if (loop_depth == 0) {
            diagnostic_error(diagnostics,
                             stmt->span,
                             "semantic",
                             "'%s' used outside of loop",
                             stmt->kind == STMT_BREAK ? "break" : "continue");
            return false;
        }
        *out_flow = stmt->kind == STMT_BREAK ? FLOW_BREAK : FLOW_CONTINUE;
        return true;
    }

    return false;
}

static bool check_statements(const Block *block,
                             SymbolTable *symbols,
                             size_t scope_start,
                             const CheckedProgram *checked,
                             size_t loop_depth,
                             DiagnosticSink *diagnostics,
                             FlowFlags *out_flow) {
    FlowFlags flow = FLOW_FALLTHROUGH;

    for (size_t i = 0; i < block->count; i++) {
        FlowFlags stmt_flow = FLOW_NONE;
        if (!check_statement(block->statements[i],
                             symbols,
                             scope_start,
                             checked,
                             loop_depth,
                             diagnostics,
                             &stmt_flow)) {
            return false;
        }
        block->statements[i]->flow_flags = stmt_flow;
        if ((flow & FLOW_FALLTHROUGH) != 0) {
            flow = (flow & ~FLOW_FALLTHROUGH) | stmt_flow;
        }
    }

    *out_flow = flow;
    return true;
}

static bool check_function(Function *function, const CheckedProgram *checked, DiagnosticSink *diagnostics) {
    SymbolTable symbols = {0};
    FlowFlags flow = FLOW_NONE;
    bool ok = true;

    for (size_t i = 0; i < function->param_count; i++) {
        Param *param = &function->params[i];
        if (!symbol_table_insert(&symbols,
                                 0,
                                 param->name,
                                 param->length,
                                 param->type,
                                 false,
                                 param->span,
                                 diagnostics,
                                 &param->symbol_id)) {
            ok = false;
            goto cleanup;
        }
    }

    if (!check_statements(&function->body,
                          &symbols,
                          symbols.count,
                          checked,
                          0,
                          diagnostics,
                          &flow)) {
        ok = false;
        goto cleanup;
    }

    function->symbol_count = symbols.next_symbol_id;
    if ((flow & FLOW_FALLTHROUGH) != 0) {
        diagnostic_error(diagnostics,
                         function->span,
                         "semantic",
                         "function '%.*s' must not fall through without returning",
                         (int) function->name_length,
                         function->name);
        ok = false;
    }

cleanup:
    free(symbols.items);
    return ok;
}

bool semantic_check_program(Program *program, DiagnosticSink *diagnostics, CheckedProgram *out_checked) {
    bool has_main = false;

    *out_checked = (CheckedProgram) {.program = program};

    for (size_t i = 0; i < program->function_count; i++) {
        Function *function = program->functions[i];
        if (lookup_function(out_checked, function->name, function->name_length, NULL) != NULL) {
            diagnostic_error(diagnostics,
                             function->span,
                             "semantic",
                             "duplicate function '%.*s'",
                             (int) function->name_length,
                             function->name);
            return false;
        }
        checked_program_append(out_checked, function);
        if (same_name(function->name, function->name_length, "main", 4)) {
            if (function->param_count != 0 || function->return_type != TYPE_I32) {
                diagnostic_error(diagnostics, function->span, "semantic", "expected fn main() -> i32");
                return false;
            }
            has_main = true;
        }
    }

    if (!has_main) {
        diagnostic_error(diagnostics, (SourceSpan) {0}, "semantic", "expected fn main() -> i32");
        return false;
    }

    for (size_t i = 0; i < program->function_count; i++) {
        if (!check_function(program->functions[i], out_checked, diagnostics)) {
            return false;
        }
    }
    return true;
}

void checked_program_destroy(CheckedProgram *checked) {
    free(checked->functions);
    *checked = (CheckedProgram) {0};
}
