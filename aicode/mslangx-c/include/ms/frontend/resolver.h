#ifndef MSLANGC_FRONTEND_RESOLVER_H_
#define MSLANGC_FRONTEND_RESOLVER_H_

#include "ms/ast.h"
#include "ms/diag.h"
#include "ms/frontend/resolution_table.h"

int ms_resolve_program(const char *file,
                       const MsAstNode *program,
                       MsResolutionTable *table,
                       MsDiagnosticList *diagnostics);

#endif