#include "semantic.h"

#include <stdio.h>
#include <string.h>

typedef struct Symbol {
    const char *name;
    size_t length;
    TypeKind type;
} Symbol;

typedef struct SymbolTable {
    Symbol items[256];
    size_t count;
} SymbolTable;

static bool same_name(const char *lhs, size_t lhs_len, const char *rhs, size_t rhs_len) {
    return lhs_len == rhs_len && strncmp(lhs, rhs, lhs_len) == 0;
}

static bool check_expr(const Expr *expr, const SymbolTable *symbols) {
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
        case EXPR_BINARY:
            return check_expr(expr->binary.lhs, symbols) && check_expr(expr->binary.rhs, symbols);
    }
    return false;
}

static bool symbol_table_insert(SymbolTable *symbols, const char *name, size_t length, TypeKind type) {
    for (size_t i = 0; i < symbols->count; i++) {
        if (same_name(name, length, symbols->items[i].name, symbols->items[i].length)) {
            fprintf(stderr, "semantic error: duplicate variable '%.*s'\n", (int) length, name);
            return false;
        }
    }
    if (symbols->count >= 256) {
        fprintf(stderr, "semantic error: too many local variables\n");
        return false;
    }
    symbols->items[symbols->count++] = (Symbol) {.name = name, .length = length, .type = type};
    return true;
}

static bool check_function(const Function *function) {
    SymbolTable symbols = {0};
    bool has_return = false;

    for (size_t i = 0; i < function->statement_count; i++) {
        const Stmt *stmt = function->statements[i];
        if (stmt->kind == STMT_LET) {
            if (!check_expr(stmt->let_stmt.value, &symbols)) {
                return false;
            }
            if (!symbol_table_insert(&symbols, stmt->let_stmt.name, stmt->let_stmt.length, stmt->let_stmt.type)) {
                return false;
            }
            continue;
        }

        if (stmt->kind == STMT_RETURN) {
            if (!check_expr(stmt->return_stmt.value, &symbols)) {
                return false;
            }
            has_return = true;
        }
    }

    if (!has_return) {
        fprintf(stderr, "semantic error: function '%.*s' must end with return\n",
                (int) function->name_length, function->name);
        return false;
    }
    return true;
}

bool semantic_check_program(const Program *program) {
    bool has_main = false;
    for (size_t i = 0; i < program->function_count; i++) {
        const Function *function = program->functions[i];
        if (same_name(function->name, function->name_length, "main", 4)) {
            has_main = true;
        }
        if (!check_function(function)) {
            return false;
        }
    }

    if (!has_main) {
        fprintf(stderr, "semantic error: expected fn main() -> i32\n");
        return false;
    }
    return true;
}
