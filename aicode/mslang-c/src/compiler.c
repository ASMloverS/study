#include "compiler_impl.h"
#include "ms/opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- error helpers ---- */

void error_at(MsCompiler* c, const MsToken* tok, const char* msg) {
    if (c->panic_mode) return;
    c->panic_mode = true;
    c->had_error  = true;
    if (c->diags && *c->diag_count < c->max_diags) {
        MsDiagnostic* d = &c->diags[(*c->diag_count)++];
        d->line   = tok->line;
        d->column = tok->column;
        snprintf(d->message, sizeof(d->message), "%s", msg);
    }
}

void error_current(MsCompiler* c, const char* msg) {
    error_at(c, &c->current, msg);
}

/* ---- advance / consume ---- */

void advance(MsCompiler* c) {
    c->previous = c->current;
    for (;;) {
        c->current = ms_scanner_next(&c->scanner);
        if (c->current.type != MS_TK_ERROR) break;
        error_current(c, c->current.start);
    }
}

void consume(MsCompiler* c, MsTokenType type, const char* msg) {
    if (c->current.type == type) { advance(c); return; }
    error_current(c, msg);
}

bool check(const MsCompiler* c, MsTokenType t) {
    return c->current.type == t;
}

bool match_tok(MsCompiler* c, MsTokenType t) {
    if (!check(c, t)) return false;
    advance(c);
    return true;
}

/* ---- emit ---- */

int emit(MsCompiler* c, MsInstruction instr) {
    int line = c->previous.line;
    int col  = c->previous.column;
    ms_chunk_write(&c->function->chunk, instr, line, col);
    return c->function->chunk.code_count - 1;
}

int emit_jmp(MsCompiler* c) {
    return emit(c, ms_enc_AsBx(MS_OP_JMP, 0, 0));
}

void patch_jmp(MsCompiler* c, int jmp_idx) {
    int target = c->function->chunk.code_count;
    int offset = target - jmp_idx - 1;
    MsInstruction* instr = &c->function->chunk.code[jmp_idx];
    *instr = ms_enc_AsBx(MS_OP_JMP, 0, offset);
}

/* ---- register allocation ---- */

int alloc_reg(MsCompiler* c) {
    int r = c->next_reg++;
    if (c->next_reg > c->max_reg) c->max_reg = c->next_reg;
    if (c->next_reg > 255) { error_current(c, "Too many registers."); }
    return r;
}

void free_reg(MsCompiler* c, int reg) {
    if (reg == c->next_reg - 1) c->next_reg--;
}

/* ---- constants ---- */

int add_constant(MsCompiler* c, MsValue val) {
    return ms_chunk_add_constant(&c->function->chunk, val);
}

/* ---- materialise ExprDesc into a register ---- */

int expr_to_reg(MsCompiler* c, MsExprDesc* e) {
    if (e->kind == EDESC_REG || e->kind == EDESC_LOCAL) {
        e->kind = EDESC_REG;
        return e->idx;
    }
    int r = alloc_reg(c);
    switch (e->kind) {
    case EDESC_NIL:
        emit(c, ms_enc_ABC(MS_OP_LOADNIL, r, 0, 0));
        break;
    case EDESC_TRUE:
        emit(c, ms_enc_ABC(MS_OP_LOADTRUE, r, 0, 0));
        break;
    case EDESC_FALSE:
        emit(c, ms_enc_ABC(MS_OP_LOADFALSE, r, 0, 0));
        break;
    case EDESC_NUMBER: {
        int k = add_constant(c, MS_NUMBER_VAL(e->number));
        emit(c, ms_enc_ABx(MS_OP_LOADK, r, k));
        break;
    }
    case EDESC_INT: {
        int k = add_constant(c, MS_INT_VAL(e->integer));
        emit(c, ms_enc_ABx(MS_OP_LOADK, r, k));
        break;
    }
    case EDESC_CONST:
        emit(c, ms_enc_ABx(MS_OP_LOADK, r, e->idx));
        break;
    case EDESC_GLOBAL:
        emit(c, ms_enc_ABx(MS_OP_GETGLOBAL, r, e->idx));
        break;
    case EDESC_UPVAL:
        emit(c, ms_enc_ABx(MS_OP_GETUPVAL, r, e->idx));
        break;
    case EDESC_PROP:
        emit(c, ms_enc_ABC(MS_OP_GETPROP, r, e->prop.obj_reg,
                            MS_K_TO_RK(e->prop.key_const)));
        break;
    default:
        break;
    }
    e->kind = EDESC_REG;
    e->idx  = r;
    return r;
}

int expr_to_any_reg(MsCompiler* c, MsExprDesc* e) {
    if (e->kind == EDESC_REG || e->kind == EDESC_LOCAL) return e->idx;
    return expr_to_reg(c, e);
}

/* ---- init / entry ---- */

static void compiler_init(MsCompiler* c, MsVM* vm, const char* source,
                           MsDiagnostic* diags, int* diag_count, int max_diags) {
    ms_scanner_init(&c->scanner, source);
    c->had_error   = false;
    c->panic_mode  = false;
    c->diags       = diags;
    c->diag_count  = diag_count;
    c->max_diags   = max_diags;
    c->vm          = vm;
    c->function    = ms_obj_function_new(vm);
    c->enclosing   = NULL;
    c->local_count = 0;
    c->scope_depth = 0;
    c->next_reg    = 0;
    c->max_reg     = 0;
    c->loop        = NULL;
    c->klass       = NULL;
    ms_table_init(&c->string_cache);
    advance(c);
}

/* ---- forward declaration for compiler_expr.c ---- */
void compile_expression_stmt(MsCompiler* c);

MsObjFunction* ms_compile(MsVM* vm, const char* source, const char* path,
                           MsDiagnostic* diags, int* diag_count, int max_diags) {
    MsCompiler c;
    compiler_init(&c, vm, source, diags, diag_count, max_diags);
    MS_UNUSED(path);

    while (!check(&c, MS_TK_EOF_TOKEN)) {
        if (match_tok(&c, MS_TK_NEWLINE) || match_tok(&c, MS_TK_SEMICOLON))
            continue;
        compile_expression_stmt(&c);
    }

    emit(&c, ms_enc_ABC(MS_OP_RETURN, 0, 0, 0));
    c.function->max_stack_size = c.max_reg;
    ms_table_free(&c.string_cache);

    return c.had_error ? NULL : c.function;
}
