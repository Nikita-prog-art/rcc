#ifndef RCC_SEMANTIC_H
#define RCC_SEMANTIC_H

#include "ast.h"
#include "diagnostic.h"

typedef struct CheckedFunction {
    const Function *function;
    size_t param_count;
    TypeKind return_type;
} CheckedFunction;

typedef struct CheckedProgram {
    Program *program;
    CheckedFunction *functions;
    size_t function_count;
    size_t function_capacity;
} CheckedProgram;

bool semantic_check_program(Program *program, DiagnosticSink *diagnostics, CheckedProgram *out_checked);
void checked_program_destroy(CheckedProgram *checked);

#endif
