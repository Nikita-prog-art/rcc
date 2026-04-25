#include "codegen.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Local {
    const char *name;
    size_t length;
    LLVMValueRef value;
} Local;

typedef struct CodegenContext {
    LLVMContextRef llvm_context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    Local locals[256];
    size_t local_count;
} CodegenContext;

static bool same_name(const char *lhs, size_t lhs_len, const char *rhs, size_t rhs_len) {
    return lhs_len == rhs_len && strncmp(lhs, rhs, lhs_len) == 0;
}

static char *copy_name(const char *name, size_t length) {
    char *buffer = malloc(length + 1);
    if (buffer == NULL) {
        return NULL;
    }
    memcpy(buffer, name, length);
    buffer[length] = '\0';
    return buffer;
}

static LLVMTypeRef llvm_type(TypeKind type, LLVMContextRef llvm_context) {
    switch (type) {
        case TYPE_I32:
            return LLVMInt32TypeInContext(llvm_context);
    }
    return LLVMInt32TypeInContext(llvm_context);
}

static LLVMValueRef lookup_local(const CodegenContext *context, const char *name, size_t length) {
    for (size_t i = 0; i < context->local_count; i++) {
        if (same_name(name, length, context->locals[i].name, context->locals[i].length)) {
            return context->locals[i].value;
        }
    }
    return NULL;
}

static bool define_local(CodegenContext *context, const char *name, size_t length, LLVMValueRef value) {
    if (context->local_count >= 256) {
        fprintf(stderr, "codegen error: too many locals\n");
        return false;
    }
    context->locals[context->local_count++] = (Local) {.name = name, .length = length, .value = value};
    return true;
}

static LLVMValueRef emit_expr(CodegenContext *context, const Expr *expr) {
    switch (expr->kind) {
        case EXPR_INTEGER:
            return LLVMConstInt(LLVMInt32TypeInContext(context->llvm_context), (unsigned long long) expr->integer_value, true);
        case EXPR_NAME: {
            LLVMValueRef value = lookup_local(context, expr->name.name, expr->name.length);
            if (value == NULL) {
                fprintf(stderr, "codegen error: undefined variable '%.*s'\n", (int) expr->name.length, expr->name.name);
            }
            return value;
        }
        case EXPR_BINARY: {
            LLVMValueRef lhs = emit_expr(context, expr->binary.lhs);
            LLVMValueRef rhs = emit_expr(context, expr->binary.rhs);
            if (lhs == NULL || rhs == NULL) {
                return NULL;
            }
            switch (expr->binary.op) {
                case BINARY_ADD:
                    return LLVMBuildAdd(context->builder, lhs, rhs, "addtmp");
                case BINARY_SUB:
                    return LLVMBuildSub(context->builder, lhs, rhs, "subtmp");
                case BINARY_MUL:
                    return LLVMBuildMul(context->builder, lhs, rhs, "multmp");
                case BINARY_DIV:
                    return LLVMBuildSDiv(context->builder, lhs, rhs, "divtmp");
            }
        }
    }
    return NULL;
}

static bool emit_function(CodegenContext *context, const Function *function) {
    LLVMTypeRef function_type = LLVMFunctionType(
        llvm_type(function->return_type, context->llvm_context),
        NULL,
        0,
        false);
    char *name = copy_name(function->name, function->name_length);
    if (name == NULL) {
        return false;
    }
    LLVMValueRef llvm_function = LLVMAddFunction(context->module, name, function_type);
    free(name);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, "entry");
    LLVMPositionBuilderAtEnd(context->builder, entry);
    context->local_count = 0;

    for (size_t i = 0; i < function->statement_count; i++) {
        const Stmt *stmt = function->statements[i];
        if (stmt->kind == STMT_LET) {
            LLVMValueRef value = emit_expr(context, stmt->let_stmt.value);
            if (value == NULL || !define_local(context, stmt->let_stmt.name, stmt->let_stmt.length, value)) {
                return false;
            }
            continue;
        }

        if (stmt->kind == STMT_RETURN) {
            LLVMValueRef value = emit_expr(context, stmt->return_stmt.value);
            if (value == NULL) {
                return false;
            }
            LLVMBuildRet(context->builder, value);
            break;
        }
    }

    if (LLVMVerifyFunction(llvm_function, LLVMPrintMessageAction) != 0) {
        fprintf(stderr, "codegen error: function verification failed\n");
        return false;
    }
    return true;
}

bool codegen_emit_ir(const Program *program, const char *output_path) {
    bool ok = false;
    CodegenContext context = {0};

    context.llvm_context = LLVMContextCreate();
    context.module = LLVMModuleCreateWithNameInContext("rcc", context.llvm_context);
    context.builder = LLVMCreateBuilderInContext(context.llvm_context);

    for (size_t i = 0; i < program->function_count; i++) {
        if (!emit_function(&context, program->functions[i])) {
            goto cleanup;
        }
    }

    if (LLVMVerifyModule(context.module, LLVMPrintMessageAction, NULL) != 0) {
        fprintf(stderr, "codegen error: module verification failed\n");
        goto cleanup;
    }

    if (LLVMPrintModuleToFile(context.module, output_path, NULL) != 0) {
        fprintf(stderr, "codegen error: failed to write IR to %s\n", output_path);
        goto cleanup;
    }

    ok = true;

cleanup:
    if (context.builder != NULL) {
        LLVMDisposeBuilder(context.builder);
    }
    if (context.module != NULL) {
        LLVMDisposeModule(context.module);
    }
    if (context.llvm_context != NULL) {
        LLVMContextDispose(context.llvm_context);
    }
    return ok;
}
