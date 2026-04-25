#include "semantic.h"

#include <stdio.h>
#include <string.h>

typedef struct Symbol {
    const char *name;
    size_t length;
    TypeKind type;
    bool is_mutable;
} Symbol;

typedef struct SymbolTable {
    Symbol items[256];
    size_t count;
} SymbolTable;

typedef struct FunctionSignature {
    const char *name;
    size_t length;
    size_t param_count;
} FunctionSignature;

typedef struct FunctionTable {
    FunctionSignature items[256];
    size_t count;
} FunctionTable;

static bool same_name(const char *lhs, size_t lhs_len, const char *rhs, size_t rhs_len) {
    return lhs_len == rhs_len && strncmp(lhs, rhs, lhs_len) == 0;
}

static const FunctionSignature *lookup_function(const FunctionTable *functions, const char *name, size_t length) {
    for (size_t i = 0; i < functions->count; i++) {
        if (same_name(name, length, functions->items[i].name, functions->items[i].length)) {
            return &functions->items[i];
        }
    }
    return NULL;
}

static bool check_expr(const Expr *expr, const SymbolTable *symbols, const FunctionTable *functions) {
    switch (expr->kind) {
        case EXPR_INTEGER:
            return true;
        case EXPR_NAME:
            for (size_t i = 0; i < symbols->count; i++) {
                if (same_name(expr->name.name, expr->name.length, symbols->items[i].name, symbols->items[i].length)) {
                    return true;
                }
            }
            fprintf(stderr, "semantic error: use of undefined variable '%.*s'\n", (int) expr->name.length, expr->name.name);
            return false;
        case EXPR_UNARY:
            return check_expr(expr->unary.operand, symbols, functions);
        case EXPR_BINARY:
            return check_expr(expr->binary.lhs, symbols, functions) && check_expr(expr->binary.rhs, symbols, functions);
        case EXPR_CALL: {
            const FunctionSignature *signature =
                lookup_function(functions, expr->call.callee, expr->call.callee_length);
            if (signature == NULL) {
                fprintf(stderr, "semantic error: call to undefined function '%.*s'\n",
                        (int) expr->call.callee_length, expr->call.callee);
                return false;
            }
            if (signature->param_count != expr->call.arg_count) {
                fprintf(stderr, "semantic error: function '%.*s' expects %zu args, got %zu\n",
                        (int) expr->call.callee_length, expr->call.callee,
                        signature->param_count, expr->call.arg_count);
                return false;
            }
            for (size_t i = 0; i < expr->call.arg_count; i++) {
                if (!check_expr(expr->call.args[i], symbols, functions)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

static bool check_statements(const Stmt *const *statements,
                             size_t statement_count,
                             const SymbolTable *input_symbols,
                             const FunctionTable *functions,
                             size_t loop_depth,
                             bool *always_returns);

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
                                bool is_mutable) {
    for (size_t i = scope_start; i < symbols->count; i++) {
        if (same_name(name, length, symbols->items[i].name, symbols->items[i].length)) {
            fprintf(stderr, "semantic error: duplicate variable '%.*s'\n", (int) length, name);
            return false;
        }
    }
    if (symbols->count >= 256) {
        fprintf(stderr, "semantic error: too many local variables\n");
        return false;
    }
    symbols->items[symbols->count++] = (Symbol) {
        .name = name,
        .length = length,
        .type = type,
        .is_mutable = is_mutable
    };
    return true;
}

static bool check_statement(const Stmt *stmt,
                            SymbolTable *symbols,
                            size_t scope_start,
                            const FunctionTable *functions,
                            size_t loop_depth,
                            bool *always_returns) {
    if (stmt->kind == STMT_LET) {
        if (!check_expr(stmt->let_stmt.value, symbols, functions)) {
            return false;
        }
        if (!symbol_table_insert(symbols,
                                 scope_start,
                                 stmt->let_stmt.name,
                                 stmt->let_stmt.length,
                                 stmt->let_stmt.type,
                                 stmt->let_stmt.is_mutable)) {
            return false;
        }
        *always_returns = false;
        return true;
    }

    if (stmt->kind == STMT_ASSIGN) {
        const Symbol *symbol = lookup_symbol(symbols, stmt->assign_stmt.name, stmt->assign_stmt.length);
        if (symbol == NULL) {
            fprintf(stderr, "semantic error: assignment to undefined variable '%.*s'\n",
                    (int) stmt->assign_stmt.length, stmt->assign_stmt.name);
            return false;
        }
        if (!symbol->is_mutable) {
            fprintf(stderr, "semantic error: variable '%.*s' is not mutable\n",
                    (int) stmt->assign_stmt.length, stmt->assign_stmt.name);
            return false;
        }
        if (!check_expr(stmt->assign_stmt.value, symbols, functions)) {
            return false;
        }
        *always_returns = false;
        return true;
    }

    if (stmt->kind == STMT_RETURN) {
        if (!check_expr(stmt->return_stmt.value, symbols, functions)) {
            return false;
        }
        *always_returns = true;
        return true;
    }

    if (stmt->kind == STMT_IF) {
        bool then_returns = false;
        bool else_returns = false;
        SymbolTable then_symbols = *symbols;
        SymbolTable else_symbols = *symbols;

        if (!check_expr(stmt->if_stmt.condition, symbols, functions)) {
            return false;
        }
        if (!check_statements((const Stmt *const *) stmt->if_stmt.then_statements,
                              stmt->if_stmt.then_count,
                              &then_symbols,
                              functions,
                              loop_depth,
                              &then_returns)) {
            return false;
        }
        if (!check_statements((const Stmt *const *) stmt->if_stmt.else_statements,
                              stmt->if_stmt.else_count,
                              &else_symbols,
                              functions,
                              loop_depth,
                              &else_returns)) {
            return false;
        }
        *always_returns = then_returns && else_returns;
        return true;
    }

    if (stmt->kind == STMT_WHILE) {
        bool body_returns = false;
        SymbolTable body_symbols = *symbols;
        if (!check_expr(stmt->while_stmt.condition, symbols, functions)) {
            return false;
        }
        if (!check_statements((const Stmt *const *) stmt->while_stmt.body_statements,
                              stmt->while_stmt.body_count,
                              &body_symbols,
                              functions,
                              loop_depth + 1,
                              &body_returns)) {
            return false;
        }
        *always_returns = false;
        return true;
    }

    if (stmt->kind == STMT_LOOP) {
        bool body_returns = false;
        SymbolTable body_symbols = *symbols;
        if (!check_statements((const Stmt *const *) stmt->loop_stmt.body_statements,
                              stmt->loop_stmt.body_count,
                              &body_symbols,
                              functions,
                              loop_depth + 1,
                              &body_returns)) {
            return false;
        }
        *always_returns = false;
        return true;
    }

    if (stmt->kind == STMT_BLOCK) {
        SymbolTable block_symbols = *symbols;
        if (!check_statements((const Stmt *const *) stmt->block_stmt.statements,
                              stmt->block_stmt.statement_count,
                              &block_symbols,
                              functions,
                              loop_depth,
                              always_returns)) {
            return false;
        }
        return true;
    }

    if (stmt->kind == STMT_EMPTY) {
        *always_returns = false;
        return true;
    }

    if (stmt->kind == STMT_BREAK || stmt->kind == STMT_CONTINUE) {
        if (loop_depth == 0) {
            fprintf(stderr, "semantic error: '%s' used outside of loop\n",
                    stmt->kind == STMT_BREAK ? "break" : "continue");
            return false;
        }
        *always_returns = true;
        return true;
    }

    return false;
}

static bool check_statements(const Stmt *const *statements,
                             size_t statement_count,
                             const SymbolTable *input_symbols,
                             const FunctionTable *functions,
                             size_t loop_depth,
                             bool *always_returns) {
    SymbolTable symbols = *input_symbols;
    size_t scope_start = input_symbols->count;
    *always_returns = false;

    for (size_t i = 0; i < statement_count; i++) {
        bool stmt_returns = false;
        if (!check_statement(statements[i], &symbols, scope_start, functions, loop_depth, &stmt_returns)) {
            return false;
        }
        if (stmt_returns) {
            *always_returns = true;
            return true;
        }
    }
    return true;
}

static bool check_function(const Function *function, const FunctionTable *functions) {
    SymbolTable symbols = {0};
    bool always_returns = false;

    for (size_t i = 0; i < function->param_count; i++) {
        const Param *param = &function->params[i];
        if (!symbol_table_insert(&symbols, 0, param->name, param->length, param->type, false)) {
            return false;
        }
    }

    if (!check_statements((const Stmt *const *) function->statements,
                          function->statement_count,
                          &symbols,
                          functions,
                          0,
                          &always_returns)) {
        return false;
    }

    if (!always_returns) {
        fprintf(stderr, "semantic error: function '%.*s' must end with return\n",
                (int) function->name_length, function->name);
        return false;
    }
    return true;
}

bool semantic_check_program(const Program *program) {
    bool has_main = false;
    FunctionTable functions = {0};

    for (size_t i = 0; i < program->function_count; i++) {
        const Function *function = program->functions[i];
        if (lookup_function(&functions, function->name, function->name_length) != NULL) {
            fprintf(stderr, "semantic error: duplicate function '%.*s'\n",
                    (int) function->name_length, function->name);
            return false;
        }
        functions.items[functions.count++] = (FunctionSignature) {
            .name = function->name,
            .length = function->name_length,
            .param_count = function->param_count
        };
        if (same_name(function->name, function->name_length, "main", 4)) {
            has_main = true;
        }
    }

    if (!has_main) {
        fprintf(stderr, "semantic error: expected fn main() -> i32\n");
        return false;
    }

    for (size_t i = 0; i < program->function_count; i++) {
        if (!check_function(program->functions[i], &functions)) {
            return false;
        }
    }
    return true;
}
