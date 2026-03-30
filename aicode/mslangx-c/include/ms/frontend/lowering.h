#ifndef MSLANGC_FRONTEND_LOWERING_H_
#define MSLANGC_FRONTEND_LOWERING_H_

#include "ms/ast.h"
#include "ms/diag.h"
#include "ms/frontend/resolution_table.h"
#include "ms/runtime/chunk.h"

typedef enum MsCompileResult {
  MS_COMPILE_RESULT_OK,
  MS_COMPILE_RESULT_PARSE_ERROR,
  MS_COMPILE_RESULT_RESOLVE_ERROR,
  MS_COMPILE_RESULT_LOWER_ERROR
} MsCompileResult;

int ms_lower_program(const char *file,
                     const MsAstNode *program,
                     const MsResolutionTable *table,
                     MsChunk *chunk,
                     MsDiagnosticList *diagnostics);
MsCompileResult ms_compile_source(const char *file,
                                  const char *source,
                                  MsChunk *chunk,
                                  MsDiagnosticList *diagnostics);

#endif