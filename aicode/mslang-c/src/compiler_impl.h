#pragma once
#include "ms/compiler.h"
#include "ms/scanner.h"
#include "ms/table.h"

typedef enum {
    EDESC_VOID, EDESC_NIL, EDESC_TRUE, EDESC_FALSE,
    EDESC_NUMBER, EDESC_INT, EDESC_CONST,
    EDESC_REG, EDESC_GLOBAL, EDESC_LOCAL, EDESC_UPVAL, EDESC_PROP,
} MsExprKind;

typedef struct {
    MsExprKind kind;
    union {
        double number;
        ms_i64 integer;
        int    idx;
        struct { int obj_reg; int key_const; } prop;
    };
    int true_list;
    int false_list;
} MsExprDesc;

#define NO_JUMP (-1)

typedef struct {
    MsToken name;
    int     depth;
    bool    is_captured;
    int     slot;
} MsLocal;

typedef struct {
    bool is_local;
    int  index;
} MsUpvalueDesc;

typedef struct MsLoopCtx {
    int              start;
    int              break_list;
    int              depth;
    struct MsLoopCtx* enclosing;
} MsLoopCtx;

typedef struct MsClassCompiler {
    struct MsClassCompiler* enclosing;
    bool                    has_superclass;
} MsClassCompiler;

typedef struct MsCompiler MsCompiler;
typedef void (*MsParseFn)(MsCompiler*, bool can_assign);

typedef enum {
    PREC_NONE = 0, PREC_ASSIGNMENT, PREC_TERNARY, PREC_OR, PREC_AND,
    PREC_EQUALITY, PREC_COMPARISON, PREC_BIT_OR, PREC_BIT_XOR,
    PREC_BIT_AND, PREC_SHIFT, PREC_TERM, PREC_FACTOR,
    PREC_UNARY, PREC_CALL, PREC_PRIMARY,
} MsPrecedence;

typedef struct {
    MsParseFn   prefix;
    MsParseFn   infix;
    MsPrecedence precedence;
} MsParseRule;

typedef struct MsCompiler {
    MsScanner        scanner;
    MsToken          current;
    MsToken          previous;
    bool             had_error;
    bool             panic_mode;
    MsDiagnostic*    diags;
    int*             diag_count;
    int              max_diags;
    MsVM*            vm;
    MsObjFunction*   function;
    struct MsCompiler* enclosing;
    MsLocal          locals[256];
    int              local_count;
    int              scope_depth;
    MsUpvalueDesc    upvalues[MS_MAX_UPVALUES];
    int              next_reg;
    int              max_reg;
    MsLoopCtx*       loop;
    MsClassCompiler* klass;
    MsTable          string_cache;
} MsCompiler;

/* Declared in compiler.c, used by compiler_expr.c */
void          advance(MsCompiler* c);
void          consume(MsCompiler* c, MsTokenType type, const char* msg);
int           emit(MsCompiler* c, MsInstruction instr);
int           alloc_reg(MsCompiler* c);
void          free_reg(MsCompiler* c, int reg);
int           expr_to_reg(MsCompiler* c, MsExprDesc* e);
int           expr_to_any_reg(MsCompiler* c, MsExprDesc* e);
int           add_constant(MsCompiler* c, MsValue val);
void          stmt_reg_reset(MsCompiler* c);
int           add_string_constant(MsCompiler* c, const char* chars, int len);
MsExprDesc    parse_precedence(MsCompiler* c, MsPrecedence min);
MsExprDesc    expression(MsCompiler* c);
bool          check(const MsCompiler* c, MsTokenType t);
bool          match_tok(MsCompiler* c, MsTokenType t);
int           emit_jmp(MsCompiler* c);
void          patch_jmp(MsCompiler* c, int jmp_idx);
void          error_at(MsCompiler* c, const MsToken* tok, const char* msg);
void          error_current(MsCompiler* c, const char* msg);
const MsParseRule* get_rule(MsTokenType t);   /* alias for rule_at, external use */
