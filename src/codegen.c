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
    LLVMValueRef functions[256];
    LLVMTypeRef function_types[256];
    const Function *function_defs[256];
    size_t function_count;
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

static LLVMValueRef lookup_function(const CodegenContext *context, const char *name, size_t length) {
    for (size_t i = 0; i < context->function_count; i++) {
        const Function *function = context->function_defs[i];
        if (same_name(name, length, function->name, function->name_length)) {
            return context->functions[i];
        }
    }
    return NULL;
}

static LLVMTypeRef lookup_function_type(const CodegenContext *context, const char *name, size_t length) {
    for (size_t i = 0; i < context->function_count; i++) {
        const Function *function = context->function_defs[i];
        if (same_name(name, length, function->name, function->name_length)) {
            return context->function_types[i];
        }
    }
    return NULL;
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
            return NULL;
        }
        case EXPR_CALL: {
            LLVMValueRef callee = lookup_function(context, expr->call.callee, expr->call.callee_length);
            LLVMTypeRef callee_type = lookup_function_type(context, expr->call.callee, expr->call.callee_length);
            LLVMValueRef args[256];
            if (callee == NULL || callee_type == NULL) {
                fprintf(stderr, "codegen error: undefined function '%.*s'\n",
                        (int) expr->call.callee_length, expr->call.callee);
                return NULL;
            }
            if (expr->call.arg_count > 256) {
                fprintf(stderr, "codegen error: too many call args\n");
                return NULL;
            }
            for (size_t i = 0; i < expr->call.arg_count; i++) {
                args[i] = emit_expr(context, expr->call.args[i]);
                if (args[i] == NULL) {
                    return NULL;
                }
            }
            return LLVMBuildCall2(
                context->builder,
                callee_type,
                callee,
                args,
                (unsigned) expr->call.arg_count,
                "calltmp");
        }
    }
    return NULL;
}

static LLVMValueRef declare_function(CodegenContext *context, const Function *function, LLVMTypeRef *out_type) {
    LLVMTypeRef param_types[256];
    for (size_t i = 0; i < function->param_count; i++) {
        param_types[i] = llvm_type(function->params[i].type, context->llvm_context);
    }
    LLVMTypeRef function_type = LLVMFunctionType(
        llvm_type(function->return_type, context->llvm_context),
        param_types,
        (unsigned) function->param_count,
        false);
    char *name = copy_name(function->name, function->name_length);
    if (name == NULL) {
        return NULL;
    }
    LLVMValueRef llvm_function = LLVMAddFunction(context->module, name, function_type);
    free(name);
    *out_type = function_type;
    return llvm_function;
}

static bool emit_function(CodegenContext *context, const Function *function, LLVMValueRef llvm_function) {

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, "entry");
    LLVMPositionBuilderAtEnd(context->builder, entry);
    context->local_count = 0;

    for (size_t i = 0; i < function->param_count; i++) {
        LLVMValueRef param = LLVMGetParam(llvm_function, (unsigned) i);
        if (!define_local(context, function->params[i].name, function->params[i].length, param)) {
            return false;
        }
    }

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
        LLVMTypeRef function_type = NULL;
        LLVMValueRef llvm_function = declare_function(&context, program->functions[i], &function_type);
        if (llvm_function == NULL) {
            goto cleanup;
        }
        context.function_defs[context.function_count] = program->functions[i];
        context.functions[context.function_count] = llvm_function;
        context.function_types[context.function_count] = function_type;
        context.function_count++;
    }

    for (size_t i = 0; i < context.function_count; i++) {
        if (!emit_function(&context, context.function_defs[i], context.functions[i])) {
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
