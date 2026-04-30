#ifndef RCC_CODEGEN_H
#define RCC_CODEGEN_H

#include "diagnostic.h"
#include "semantic.h"

bool codegen_emit_ir(const CheckedProgram *checked, const char *output_path, DiagnosticSink *diagnostics);

#endif
