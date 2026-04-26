#include "codegen.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Local {
    const char *name;
    size_t length;
    LLVMValueRef storage;
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
    LLVMBasicBlockRef loop_continue_blocks[256];
    LLVMBasicBlockRef loop_break_blocks[256];
    size_t loop_depth;
    unsigned temp_counter;
} CodegenContext;

enum {
    MAX_FUNCTIONS = 256,
    MAX_LOCALS = 256,
    MAX_LOOP_DEPTH = 256
};

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
    for (size_t i = context->local_count; i > 0; i--) {
        if (same_name(name, length, context->locals[i - 1].name, context->locals[i - 1].length)) {
            return context->locals[i - 1].storage;
        }
    }
    return NULL;
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

static bool define_local(CodegenContext *context, const char *name, size_t length, LLVMValueRef storage) {
    if (context->local_count >= MAX_LOCALS) {
        fprintf(stderr, "codegen error: too many locals\n");
        return false;
    }
    context->locals[context->local_count++] = (Local) {.name = name, .length = length, .storage = storage};
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
            LLVMValueRef storage = lookup_local(context, expr->name.name, expr->name.length);
            if (storage == NULL) {
                fprintf(stderr, "codegen error: undefined variable '%.*s'\n", (int) expr->name.length, expr->name.name);
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
    if (function->param_count > 256) {
        fprintf(stderr, "codegen error: too many function parameters\n");
        return NULL;
    }
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

static bool block_has_terminator(LLVMBasicBlockRef block) {
    return LLVMGetBasicBlockTerminator(block) != NULL;
}

static bool push_loop(CodegenContext *context, LLVMBasicBlockRef continue_block, LLVMBasicBlockRef break_block) {
    if (context->loop_depth >= MAX_LOOP_DEPTH) {
        fprintf(stderr, "codegen error: loop nesting limit exceeded\n");
        return false;
    }
    context->loop_continue_blocks[context->loop_depth] = continue_block;
    context->loop_break_blocks[context->loop_depth] = break_block;
    context->loop_depth++;
    return true;
}

static bool emit_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function);

static bool emit_statement_list(CodegenContext *context,
                                const Stmt *const *statements,
                                size_t statement_count,
                                LLVMValueRef llvm_function) {
    for (size_t i = 0; i < statement_count; i++) {
        if (!emit_statement(context, statements[i], llvm_function)) {
            return false;
        }
        if (block_has_terminator(LLVMGetInsertBlock(context->builder))) {
            break;
        }
    }
    return true;
}

static bool emit_block_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    size_t saved_local_count = context->local_count;
    if (!emit_statement_list(context,
                             (const Stmt *const *) stmt->block_stmt.statements,
                             stmt->block_stmt.statement_count,
                             llvm_function)) {
        return false;
    }
    if (!block_has_terminator(LLVMGetInsertBlock(context->builder))) {
        context->local_count = saved_local_count;
    }
    return true;
}

static bool emit_if_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    LLVMValueRef condition_value = emit_expr(context, stmt->if_stmt.condition);
    char then_name[32];
    char else_name[32];
    char merge_name[32];
    Local saved_locals[256];
    size_t saved_local_count = context->local_count;

    if (condition_value == NULL) {
        return false;
    }
    memcpy(saved_locals, context->locals, sizeof(saved_locals));
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
    context->local_count = saved_local_count;
    memcpy(context->locals, saved_locals, sizeof(saved_locals));
    if (!emit_statement_list(context,
                             (const Stmt *const *) stmt->if_stmt.then_statements,
                             stmt->if_stmt.then_count,
                             llvm_function)) {
        return false;
    }
    bool then_falls_through = !block_has_terminator(LLVMGetInsertBlock(context->builder));
    if (then_falls_through) {
        LLVMBuildBr(context->builder, merge_block);
    }

    LLVMPositionBuilderAtEnd(context->builder, else_block);
    context->local_count = saved_local_count;
    memcpy(context->locals, saved_locals, sizeof(saved_locals));
    if (!emit_statement_list(context,
                             (const Stmt *const *) stmt->if_stmt.else_statements,
                             stmt->if_stmt.else_count,
                             llvm_function)) {
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
    context->local_count = saved_local_count;
    memcpy(context->locals, saved_locals, sizeof(saved_locals));
    return true;
}

static bool emit_while_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    char cond_name[32];
    char body_name[32];
    char exit_name[32];
    Local saved_locals[256];
    size_t saved_local_count = context->local_count;

    memcpy(saved_locals, context->locals, sizeof(saved_locals));
    snprintf(cond_name, sizeof(cond_name), "while_cond_%u", context->temp_counter);
    snprintf(body_name, sizeof(body_name), "while_body_%u", context->temp_counter);
    snprintf(exit_name, sizeof(exit_name), "while_end_%u", context->temp_counter);
    context->temp_counter++;

    LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, cond_name);
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, body_name);
    LLVMBasicBlockRef exit_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, exit_name);

    LLVMBuildBr(context->builder, cond_block);

    LLVMPositionBuilderAtEnd(context->builder, cond_block);
    context->local_count = saved_local_count;
    memcpy(context->locals, saved_locals, sizeof(saved_locals));
    LLVMValueRef condition_value = emit_expr(context, stmt->while_stmt.condition);
    if (condition_value == NULL) {
        return false;
    }
    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(context->llvm_context), 0, false);
    LLVMValueRef is_true = LLVMBuildICmp(context->builder, LLVMIntNE, condition_value, zero, "whilecond");
    LLVMBuildCondBr(context->builder, is_true, body_block, exit_block);

    LLVMPositionBuilderAtEnd(context->builder, body_block);
    context->local_count = saved_local_count;
    memcpy(context->locals, saved_locals, sizeof(saved_locals));
    if (!push_loop(context, cond_block, exit_block)) {
        return false;
    }
    if (!emit_statement_list(context,
                             (const Stmt *const *) stmt->while_stmt.body_statements,
                             stmt->while_stmt.body_count,
                             llvm_function)) {
        context->loop_depth--;
        return false;
    }
    context->loop_depth--;
    if (!block_has_terminator(LLVMGetInsertBlock(context->builder))) {
        LLVMBuildBr(context->builder, cond_block);
    }

    LLVMPositionBuilderAtEnd(context->builder, exit_block);
    context->local_count = saved_local_count;
    memcpy(context->locals, saved_locals, sizeof(saved_locals));
    return true;
}

static bool emit_loop_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    char body_name[32];
    char exit_name[32];
    Local saved_locals[256];
    size_t saved_local_count = context->local_count;

    memcpy(saved_locals, context->locals, sizeof(saved_locals));
    snprintf(body_name, sizeof(body_name), "loop_body_%u", context->temp_counter);
    snprintf(exit_name, sizeof(exit_name), "loop_end_%u", context->temp_counter);
    context->temp_counter++;

    LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, body_name);
    LLVMBasicBlockRef exit_block = LLVMAppendBasicBlockInContext(context->llvm_context, llvm_function, exit_name);

    LLVMBuildBr(context->builder, body_block);

    LLVMPositionBuilderAtEnd(context->builder, body_block);
    context->local_count = saved_local_count;
    memcpy(context->locals, saved_locals, sizeof(saved_locals));
    if (!push_loop(context, body_block, exit_block)) {
        return false;
    }
    if (!emit_statement_list(context,
                             (const Stmt *const *) stmt->loop_stmt.body_statements,
                             stmt->loop_stmt.body_count,
                             llvm_function)) {
        context->loop_depth--;
        return false;
    }
    context->loop_depth--;
    if (!block_has_terminator(LLVMGetInsertBlock(context->builder))) {
        LLVMBuildBr(context->builder, body_block);
    }

    LLVMPositionBuilderAtEnd(context->builder, exit_block);
    context->local_count = saved_local_count;
    memcpy(context->locals, saved_locals, sizeof(saved_locals));
    return true;
}

static bool emit_statement(CodegenContext *context, const Stmt *stmt, LLVMValueRef llvm_function) {
    if (stmt->kind == STMT_LET) {
        LLVMValueRef value = emit_expr(context, stmt->let_stmt.value);
        char *name = copy_name(stmt->let_stmt.name, stmt->let_stmt.length);
        LLVMValueRef storage;
        if (value == NULL || name == NULL) {
            free(name);
            return false;
        }
        storage = create_entry_alloca(context, llvm_function, name);
        free(name);
        LLVMBuildStore(context->builder, value, storage);
        return define_local(context, stmt->let_stmt.name, stmt->let_stmt.length, storage);
    }

    if (stmt->kind == STMT_ASSIGN) {
        LLVMValueRef storage = lookup_local(context, stmt->assign_stmt.name, stmt->assign_stmt.length);
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
        return emit_block_statement(context, stmt, llvm_function);
    }

    if (stmt->kind == STMT_EMPTY) {
        return true;
    }

    if (stmt->kind == STMT_BREAK) {
        if (context->loop_depth == 0) {
            fprintf(stderr, "codegen error: break outside of loop\n");
            return false;
        }
        LLVMBuildBr(context->builder, context->loop_break_blocks[context->loop_depth - 1]);
        return true;
    }

    if (stmt->kind == STMT_CONTINUE) {
        if (context->loop_depth == 0) {
            fprintf(stderr, "codegen error: continue outside of loop\n");
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
    context->local_count = 0;

    for (size_t i = 0; i < function->param_count; i++) {
        LLVMValueRef param = LLVMGetParam(llvm_function, (unsigned) i);
        char *name = copy_name(function->params[i].name, function->params[i].length);
        LLVMValueRef storage;
        if (name == NULL) {
            return false;
        }
        storage = create_entry_alloca(context, llvm_function, name);
        free(name);
        LLVMBuildStore(context->builder, param, storage);
        if (!define_local(context, function->params[i].name, function->params[i].length, storage)) {
            return false;
        }
    }

    for (size_t i = 0; i < function->statement_count; i++) {
        if (!emit_statement(context, function->statements[i], llvm_function)) {
            return false;
        }
        if (block_has_terminator(LLVMGetInsertBlock(context->builder))) {
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
        if (context.function_count >= MAX_FUNCTIONS) {
            fprintf(stderr, "codegen error: too many functions\n");
            goto cleanup;
        }
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
