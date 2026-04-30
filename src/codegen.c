#include "codegen.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CodegenContext {
    LLVMContextRef llvm_context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    const CheckedProgram *checked;
    LLVMValueRef *functions;
    LLVMTypeRef *function_types;
    LLVMValueRef *locals;
    size_t local_count;
    LLVMBasicBlockRef *loop_continue_blocks;
    LLVMBasicBlockRef *loop_break_blocks;
    size_t loop_depth;
    size_t loop_capacity;
    unsigned temp_counter;
    DiagnosticSink *diagnostics;
} CodegenContext;

static void *xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
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

static char *copy_name(const char *name, size_t length) {
    char *buffer = xcalloc(length + 1, 1);
    memcpy(buffer, name, length);
    return buffer;
}

static LLVMTypeRef llvm_type(TypeKind type, LLVMContextRef llvm_context) {
    switch (type) {
        case TYPE_I32:
            return LLVMInt32TypeInContext(llvm_context);
    }
    return LLVMInt32TypeInContext(llvm_context);
}

static bool set_local(CodegenContext *context, size_t symbol_id, LLVMValueRef storage) {
    if (symbol_id >= context->local_count) {
        diagnostic_error(context->diagnostics, (SourceSpan) {0}, "codegen", "unresolved local symbol");
        return false;
    }
    context->locals[symbol_id] = storage;
    return true;
}

static LLVMValueRef get_local(CodegenContext *context, size_t symbol_id, SourceSpan span) {
    if (symbol_id >= context->local_count || context->locals[symbol_id] == NULL) {
        diagnostic_error(context->diagnostics, span, "codegen", "unresolved local symbol");
        return NULL;
    }
    return context->locals[symbol_id];
}

static LLVMValueRef create_entry_alloca(CodegenContext *context, LLVMValueRef llvm_function, const char *name) {
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(context->llvm_context);
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(llvm_function);
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    if (first != NULL) {
        LLVMPositionBuilderBefore(builder, first);
    } else {
        LLVMPositionBuilderAtEnd(builder, entry);
    }
    LLVMValueRef alloca_inst = LLVMBuildAlloca(builder, LLVMInt32TypeInContext(context->llvm_context), name);
    LLVMDisposeBuilder(builder);
    return alloca_inst;
}

static LLVMValueRef emit_expr(CodegenContext *context, const Expr *expr) {
    switch (expr->kind) {
        case EXPR_INTEGER:
            return LLVMConstInt(LLVMInt32TypeInContext(context->llvm_context), (unsigned long long) expr->integer_value, true);
        case EXPR_NAME: {
            LLVMValueRef storage = get_local(context, expr->name.symbol_id, expr->span);
            if (storage == NULL) {
                return NULL;
            }
            return LLVMBuildLoad2(
                context->builder,
                LLVMInt32TypeInContext(context->llvm_context),
                storage,
                "loadtmp");
        }
        case EXPR_UNARY: {
            LLVMValueRef operand = emit_expr(context, expr->unary.operand);
            if (operand == NULL) {
                return NULL;
            }
            switch (expr->unary.op) {
                case UNARY_NEG:
                    return LLVMBuildNeg(context->builder, operand, "negtmp");
                case UNARY_NOT: {
                    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(context->llvm_context), 0, false);
                    return LLVMBuildZExt(
                        context->builder,
                        LLVMBuildICmp(context->builder, LLVMIntEQ, operand, zero, "nottmp"),
                        LLVMInt32TypeInContext(context->llvm_context),
                        "noti32");
                }
            }
            return NULL;
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
                case BINARY_REM:
                    return LLVMBuildSRem(context->builder, lhs, rhs, "remtmp");
                case BINARY_DIV:
                    return LLVMBuildSDiv(context->builder, lhs, rhs, "divtmp");
                case BINARY_EQ:
                    return LLVMBuildZExt(
                        context->builder,
                        LLVMBuildICmp(context->builder, LLVMIntEQ, lhs, rhs, "eqtmp"),
                        LLVMInt32TypeInContext(context->llvm_context),
                        "eqi32");
                case BINARY_NE:
                    return LLVMBuildZExt(
                        context->builder,
                        LLVMBuildICmp(context->builder, LLVMIntNE, lhs, rhs, "netmp"),
                        LLVMInt32TypeInContext(context->llvm_context),
                        "nei32");
                case BINARY_LT:
                    return LLVMBuildZExt(
                        context->builder,
                        LLVMBuildICmp(context->builder, LLVMIntSLT, lhs, rhs, "lttmp"),
                        LLVMInt32TypeInContext(context->llvm_context),
                        "lti32");
                case BINARY_LE:
                    return LLVMBuildZExt(
                        context->builder,
                        LLVMBuildICmp(context->builder, LLVMIntSLE, lhs, rhs, "letmp"),
                        LLVMInt32TypeInContext(context->llvm_context),
                        "lei32");
                case BINARY_GT:
                    return LLVMBuildZExt(
                        context->builder,
                        LLVMBuildICmp(context->builder, LLVMIntSGT, lhs, rhs, "gttmp"),
                        LLVMInt32TypeInContext(context->llvm_context),
                        "gti32");
                case BINARY_GE:
                    return LLVMBuildZExt(
                        context->builder,
                        LLVMBuildICmp(context->builder, LLVMIntSGE, lhs, rhs, "getmp"),
                        LLVMInt32TypeInContext(context->llvm_context),
                        "gei32");
            }
            return NULL;
        }
        case EXPR_CALL: {
            LLVMValueRef callee = NULL;
            LLVMTypeRef callee_type = NULL;
            LLVMValueRef *args = xcalloc(expr->call.arg_count, sizeof(LLVMValueRef));
            LLVMValueRef result = NULL;

            if (expr->call.function_id >= context->checked->function_count) {
                diagnostic_error(context->diagnostics, expr->span, "codegen", "unresolved function call");
                goto cleanup;
            }
            callee = context->functions[expr->call.function_id];
            callee_type = context->function_types[expr->call.function_id];
            if (callee == NULL || callee_type == NULL) {
                diagnostic_error(context->diagnostics, expr->span, "codegen", "unresolved function call");
                goto cleanup;
            }
            for (size_t i = 0; i < expr->call.arg_count; i++) {
                args[i] = emit_expr(context, expr->call.args[i]);
                if (args[i] == NULL) {
                    goto cleanup;
                }
            }
            result = LLVMBuildCall2(
                context->builder,
                callee_type,
                callee,
                args,
                (unsigned) expr->call.arg_count,
                "calltmp");

        cleanup:
            free(args);
            return result;
        }
    }
    return NULL;
}

static LLVMValueRef declare_function(CodegenContext *context, const Function *function, LLVMTypeRef *out_type) {
    LLVMTypeRef *param_types = xcalloc(function->param_count, sizeof(LLVMTypeRef));
    for (size_t i = 0; i < function->param_count; i++) {
        param_types[i] = llvm_type(function->params[i].type, context->llvm_context);
    }
    LLVMTypeRef function_type = LLVMFunctionType(
        llvm_type(function->return_type, context->llvm_context),
        function->param_count == 0 ? NULL : param_types,
        (unsigned) function->param_count,
        false);
    char *name = copy_name(function->name, function->name_length);
    LLVMValueRef llvm_function = LLVMAddFunction(context->module, name, function_type);
    free(name);
    free(param_types);
    *out_type = function_type;
    return llvm_function;
}

static bool block_has_terminator(LLVMBasicBlockRef block) {
    return LLVMGetBasicBlockTerminator(block) != NULL;
}

static bool push_loop(CodegenContext *context, LLVMBasicBlockRef continue_block, LLVMBasicBlockRef break_block) {
    if (context->loop_depth >= context->loop_capacity) {
        size_t next_capacity = context->loop_capacity == 0 ? 8 : context->loop_capacity * 2;
        context->loop_continue_blocks =
            xrealloc(context->loop_continue_blocks, next_capacity * sizeof(LLVMBasicBlockRef));
        context->loop_break_blocks =
            xrealloc(context->loop_break_blocks, next_capacity * sizeof(LLVMBasicBlockRef));
        context->loop_capacity = next_capacity;
    }
    context->loop_continue_blocks[context->loop_depth] = continue_block;
    context->loop_break_blocks[context->loop_depth] = break_block;
    context->loop_depth++;
    return true;
}

static bool emit_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function);

static bool emit_statement_list(CodegenContext *context, const Block *block, LLVMValueRef llvm_function) {
    for (size_t i = 0; i < block->count; i++) {
        if (!emit_statement(context, block->statements[i], llvm_function)) {
            return false;
        }
        if (block_has_terminator(LLVMGetInsertBlock(context->builder))) {
            break;
        }
    }
    return true;
}

static bool emit_if_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    LLVMValueRef condition_value = emit_expr(context, stmt->if_stmt.condition);
    char then_name[32];
    char else_name[32];
    char merge_name[32];

    if (condition_value == NULL) {
        return false;
    }
    snprintf(then_name, sizeof(then_name), "then_%u", context->temp_counter);
    snprintf(else_name, sizeof(else_name), "else_%u", context->temp_counter);
    snprintf(merge_name, sizeof(merge_name), "ifend_%u", context->temp_counter);
    context->temp_counter++;

    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(context->llvm_context), 0, false);
    LLVMValueRef is_true = LLVMBuildICmp(context->builder, LLVMIntNE, condition_value, zero, "ifcond");
    LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, then_name);
    LLVMBasicBlockRef else_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, else_name);
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, merge_name);

    LLVMBuildCondBr(context->builder, is_true, then_block, else_block);

    LLVMPositionBuilderAtEnd(context->builder, then_block);
    if (!emit_statement_list(context, &stmt->if_stmt.then_block, llvm_function)) {
        return false;
    }
    bool then_falls_through = !block_has_terminator(LLVMGetInsertBlock(context->builder));
    if (then_falls_through) {
        LLVMBuildBr(context->builder, merge_block);
    }

    LLVMPositionBuilderAtEnd(context->builder, else_block);
    if (!emit_statement_list(context, &stmt->if_stmt.else_block, llvm_function)) {
        return false;
    }
    bool else_falls_through = !block_has_terminator(LLVMGetInsertBlock(context->builder));
    if (else_falls_through) {
        LLVMBuildBr(context->builder, merge_block);
    }

    if (!then_falls_through && !else_falls_through) {
        LLVMDeleteBasicBlock(merge_block);
        return true;
    }

    LLVMPositionBuilderAtEnd(context->builder, merge_block);
    return true;
}

static bool emit_while_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    char cond_name[32];
    char body_name[32];
    char exit_name[32];

    snprintf(cond_name, sizeof(cond_name), "while_cond_%u", context->temp_counter);
    snprintf(body_name, sizeof(body_name), "while_body_%u", context->temp_counter);
    snprintf(exit_name, sizeof(exit_name), "while_end_%u", context->temp_counter);
    context->temp_counter++;

    LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, cond_name);
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, body_name);
    LLVMBasicBlockRef exit_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, exit_name);

    LLVMBuildBr(context->builder, cond_block);

    LLVMPositionBuilderAtEnd(context->builder, cond_block);
    LLVMValueRef condition_value = emit_expr(context, stmt->while_stmt.condition);
    if (condition_value == NULL) {
        return false;
    }
    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(context->llvm_context), 0, false);
    LLVMValueRef is_true = LLVMBuildICmp(context->builder, LLVMIntNE, condition_value, zero, "whilecond");
    LLVMBuildCondBr(context->builder, is_true, body_block, exit_block);

    LLVMPositionBuilderAtEnd(context->builder, body_block);
    if (!push_loop(context, cond_block, exit_block)) {
        return false;
    }
    if (!emit_statement_list(context, &stmt->while_stmt.body, llvm_function)) {
        context->loop_depth--;
        return false;
    }
    context->loop_depth--;
    if (!block_has_terminator(LLVMGetInsertBlock(context->builder))) {
        LLVMBuildBr(context->builder, cond_block);
    }

    LLVMPositionBuilderAtEnd(context->builder, exit_block);
    return true;
}

static bool emit_loop_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    char body_name[32];
    char exit_name[32];

    snprintf(body_name, sizeof(body_name), "loop_body_%u", context->temp_counter);
    snprintf(exit_name, sizeof(exit_name), "loop_end_%u", context->temp_counter);
    context->temp_counter++;

    LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, body_name);
    LLVMBasicBlockRef exit_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, exit_name);

    LLVMBuildBr(context->builder, body_block);

    LLVMPositionBuilderAtEnd(context->builder, body_block);
    if (!push_loop(context, body_block, exit_block)) {
        return false;
    }
    if (!emit_statement_list(context, &stmt->loop_stmt.body, llvm_function)) {
        context->loop_depth--;
        return false;
    }
    context->loop_depth--;
    if (!block_has_terminator(LLVMGetInsertBlock(context->builder))) {
        LLVMBuildBr(context->builder, body_block);
    }

    if ((stmt->flow_flags & FLOW_FALLTHROUGH) == 0) {
        LLVMDeleteBasicBlock(exit_block);
        return true;
    }

    LLVMPositionBuilderAtEnd(context->builder, exit_block);
    return true;
}

static bool emit_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    if (stmt->kind == STMT_LET) {
        LLVMValueRef value = emit_expr(context, stmt->let_stmt.value);
        char *name = copy_name(stmt->let_stmt.name, stmt->let_stmt.length);
        LLVMValueRef storage;
        if (value == NULL) {
            free(name);
            return false;
        }
        storage = create_entry_alloca(context, llvm_function, name);
        free(name);
        LLVMBuildStore(context->builder, value, storage);
        return set_local(context, stmt->let_stmt.symbol_id, storage);
    }

    if (stmt->kind == STMT_ASSIGN) {
        LLVMValueRef storage = get_local(context, stmt->assign_stmt.symbol_id, stmt->span);
        LLVMValueRef value = emit_expr(context, stmt->assign_stmt.value);
        if (storage == NULL || value == NULL) {
            return false;
        }
        LLVMBuildStore(context->builder, value, storage);
        return true;
    }

    if (stmt->kind == STMT_EXPR) {
        return emit_expr(context, stmt->expr_stmt.value) != NULL;
    }

    if (stmt->kind == STMT_RETURN) {
        LLVMValueRef value = emit_expr(context, stmt->return_stmt.value);
        if (value == NULL) {
            return false;
        }
        LLVMBuildRet(context->builder, value);
        return true;
    }

    if (stmt->kind == STMT_IF) {
        return emit_if_statement(context, stmt, llvm_function);
    }

    if (stmt->kind == STMT_WHILE) {
        return emit_while_statement(context, stmt, llvm_function);
    }

    if (stmt->kind == STMT_LOOP) {
        return emit_loop_statement(context, stmt, llvm_function);
    }

    if (stmt->kind == STMT_BLOCK) {
        return emit_statement_list(context, &stmt->block_stmt.body, llvm_function);
    }

    if (stmt->kind == STMT_EMPTY) {
        return true;
    }

    if (stmt->kind == STMT_BREAK) {
        if (context->loop_depth == 0) {
            diagnostic_error(context->diagnostics, stmt->span, "codegen", "break outside of loop");
            return false;
        }
        LLVMBuildBr(context->builder, context->loop_break_blocks[context->loop_depth - 1]);
        return true;
    }

    if (stmt->kind == STMT_CONTINUE) {
        if (context->loop_depth == 0) {
            diagnostic_error(context->diagnostics, stmt->span, "codegen", "continue outside of loop");
            return false;
        }
        LLVMBuildBr(context->builder, context->loop_continue_blocks[context->loop_depth - 1]);
        return true;
    }

    return false;
}

static bool emit_function(CodegenContext *context, const Function *function, LLVMValueRef llvm_function) {
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, "entry");
    LLVMPositionBuilderAtEnd(context->builder, entry);
    context->local_count = function->symbol_count;
    context->locals = xcalloc(context->local_count, sizeof(LLVMValueRef));
    context->loop_depth = 0;

    for (size_t i = 0; i < function->param_count; i++) {
        LLVMValueRef param = LLVMGetParam(llvm_function, (unsigned) i);
        char *name = copy_name(function->params[i].name, function->params[i].length);
        LLVMValueRef storage = create_entry_alloca(context, llvm_function, name);
        free(name);
        LLVMBuildStore(context->builder, param, storage);
        if (!set_local(context, function->params[i].symbol_id, storage)) {
            return false;
        }
    }

    if (!emit_statement_list(context, &function->body, llvm_function)) {
        return false;
    }

    if (LLVMVerifyFunction(llvm_function, LLVMReturnStatusAction) != 0) {
        diagnostic_error(context->diagnostics, function->span, "codegen", "function verification failed");
        return false;
    }
    free(context->locals);
    context->locals = NULL;
    context->local_count = 0;
    return true;
}

bool codegen_emit_ir(const CheckedProgram *checked, const char *output_path, DiagnosticSink *diagnostics) {
    bool ok = false;
    char *message = NULL;
    CodegenContext context = {0};

    context.checked = checked;
    context.diagnostics = diagnostics;
    context.llvm_context = LLVMContextCreate();
    context.module = LLVMModuleCreateWithNameInContext("rcc", context.llvm_context);
    context.builder = LLVMCreateBuilderInContext(context.llvm_context);
    context.functions = xcalloc(checked->function_count, sizeof(LLVMValueRef));
    context.function_types = xcalloc(checked->function_count, sizeof(LLVMTypeRef));

    for (size_t i = 0; i < checked->function_count; i++) {
        LLVMTypeRef function_type = NULL;
        LLVMValueRef llvm_function = declare_function(&context, checked->functions[i].function, &function_type);
        if (llvm_function == NULL) {
            goto cleanup;
        }
        context.functions[i] = llvm_function;
        context.function_types[i] = function_type;
    }

    for (size_t i = 0; i < checked->function_count; i++) {
        if (!emit_function(&context, checked->functions[i].function, context.functions[i])) {
            goto cleanup;
        }
    }

    if (LLVMVerifyModule(context.module, LLVMReturnStatusAction, &message) != 0) {
        diagnostic_error(context.diagnostics,
                         (SourceSpan) {0},
                         "codegen",
                         "module verification failed: %s",
                         message == NULL ? "unknown verifier error" : message);
        goto cleanup;
    }

    if (LLVMPrintModuleToFile(context.module, output_path, &message) != 0) {
        diagnostic_error(context.diagnostics,
                         (SourceSpan) {0},
                         "codegen",
                         "failed to write IR to %s: %s",
                         output_path,
                         message == NULL ? "unknown error" : message);
        goto cleanup;
    }

    ok = true;

cleanup:
    if (message != NULL) {
        LLVMDisposeMessage(message);
    }
    free(context.locals);
    free(context.loop_continue_blocks);
    free(context.loop_break_blocks);
    free(context.functions);
    free(context.function_types);
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
