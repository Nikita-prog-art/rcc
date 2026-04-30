#include "codegen.h"
#include "parser.h"
#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "failed to open %s\n", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);

    char *buffer = calloc((size_t) size + 1, 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(buffer, 1, (size_t) size, file) != (size_t) size) {
        fclose(file);
        free(buffer);
        return NULL;
    }
    fclose(file);
    return buffer;
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s <input.rs> <output.ll>\n", argv0);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }
    DiagnosticSink diagnostics;
    diagnostic_sink_init(&diagnostics, stderr);

    char *source = read_file(argv[1]);
    if (source == NULL) {
        return 1;
    }

    Parser parser;
    parser_init(&parser, source, &diagnostics);
    Program *program = parse_program(&parser);
    free(source);
    if (program == NULL) {
        return 1;
    }

    CheckedProgram checked = {0};
    bool ok = semantic_check_program(program, &diagnostics, &checked) &&
              codegen_emit_ir(&checked, argv[2], &diagnostics);
    checked_program_destroy(&checked);
    program_destroy(program);
    return ok ? 0 : 1;
}
