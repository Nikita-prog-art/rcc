#ifndef RCC_CODEGEN_H
#define RCC_CODEGEN_H

#include "ast.h"

bool codegen_emit_ir(const Program *program, const char *output_path);

#endif
