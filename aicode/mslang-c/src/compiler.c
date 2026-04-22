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

/* Read raw sBx from an existing JMP for linked-list break chains.
   Sentinel: raw sBx == -MS_sBx_BIAS means NO_JUMP (stored as Bx=0). */
static int get_jmp_next(MsCompiler* c, int idx) {
    int raw = MS_GET_sBx(c->function->chunk.code[idx]);
    if (raw == -MS_sBx_BIAS) return NO_JUMP;
    return idx + 1 + raw;
}

/* Patch every JMP in a linked list to target_ip. */
static void patch_list(MsCompiler* c, int list, int target_ip) {
    while (list != NO_JUMP) {
        int next = get_jmp_next(c, list);
        int offset = target_ip - list - 1;
        c->function->chunk.code[list] = ms_enc_AsBx(MS_OP_JMP, 0, offset);
        list = next;
    }
}

/* Emit a JMP whose sBx encodes the previous break_list entry.
   Returns the instruction index of the new JMP.
   The new break_list head becomes this index. */
static int emit_break_jmp(MsCompiler* c, int prev_list) {
    /* Encode prev_list in sBx: use a raw value so get_jmp_next can decode.
       Raw sBx = prev_list - idx - 1 would be the offset if prev_list were target.
       Instead we store (prev_list + MS_sBx_BIAS) as Bx to avoid bias confusion.
       get_jmp_next reads raw sBx and interprets -MS_sBx_BIAS as NO_JUMP. */
    int idx = c->function->chunk.code_count;
    int encoded = (prev_list == NO_JUMP) ? -MS_sBx_BIAS : (prev_list - idx - 1);
    emit(c, ms_enc_AsBx(MS_OP_JMP, 0, encoded));
    return idx;
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

/* Reset next_reg to the first free slot above all active locals. */
void stmt_reg_reset(MsCompiler* c) {
    int base = 0;
    for (int i = 0; i < c->local_count; i++) {
        if (c->locals[i].slot >= base) base = c->locals[i].slot + 1;
    }
    c->next_reg = base;
}

/* ---- constants ---- */

int add_constant(MsCompiler* c, MsValue val) {
    return ms_chunk_add_constant(&c->function->chunk, val);
}

int add_string_constant(MsCompiler* c, const char* chars, int len) {
    MsObjString* s = ms_obj_string_copy(c->vm, chars, len);
    MsValue existing;
    if (ms_table_get(&c->string_cache, s, &existing)) {
        return (int)MS_AS_INT(existing);
    }
    int idx = ms_chunk_add_constant(&c->function->chunk, MS_OBJ_VAL(s));
    ms_table_set(&c->string_cache, s, MS_INT_VAL(idx));
    return idx;
}

/* ---- scope management ---- */

static void begin_scope(MsCompiler* c) { c->scope_depth++; }

static void end_scope(MsCompiler* c) {
    c->scope_depth--;
    while (c->local_count > 0 &&
           c->locals[c->local_count - 1].depth > c->scope_depth) {
        MsLocal* local = &c->locals[c->local_count - 1];
        if (local->is_captured)
            emit(c, ms_enc_ABC(MS_OP_CLOSE, local->slot, 0, 0));
        c->local_count--;
        free_reg(c, local->slot);
    }
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

/* ---- forward declarations for compiler_expr.c ---- */
void compile_expression_stmt(MsCompiler* c);

/* ---- var declaration ---- */

static void parse_var_decl(MsCompiler* c) {
    consume(c, MS_TK_IDENTIFIER, "Expected variable name.");
    MsToken name = c->previous;

    int rhs_reg = c->next_reg;
    if (match_tok(c, MS_TK_EQUAL)) {
        expression(c);
        rhs_reg = c->next_reg - 1;
    } else {
        int r = alloc_reg(c);
        emit(c, ms_enc_ABC(MS_OP_LOADNIL, r, 0, 0));
        rhs_reg = r;
    }

    if (c->scope_depth == 0) {
        int k = add_string_constant(c, name.start, name.length);
        emit(c, ms_enc_ABx(MS_OP_DEFGLOBAL, rhs_reg, k));
        free_reg(c, rhs_reg);
    } else {
        if (c->local_count >= 256) {
            error_at(c, &name, "Too many local variables.");
            return;
        }
        MsLocal* local = &c->locals[c->local_count++];
        local->name        = name;
        local->depth       = c->scope_depth;
        local->is_captured = false;
        local->slot        = rhs_reg;
    }
}

/* ---- block statement ---- */

static void compile_statement(MsCompiler* c);

static void parse_block(MsCompiler* c) {
    begin_scope(c);
    while (!check(c, MS_TK_RIGHT_BRACE) && !check(c, MS_TK_EOF_TOKEN)) {
        if (match_tok(c, MS_TK_NEWLINE) || match_tok(c, MS_TK_SEMICOLON))
            continue;
        compile_statement(c);
    }
    consume(c, MS_TK_RIGHT_BRACE, "Expected '}' after block.");
    end_scope(c);
}

/* ---- if / else ---- */

/* Parse either a braced block or a single statement (braceless body). */
static void parse_body(MsCompiler* c) {
    match_tok(c, MS_TK_NEWLINE);
    if (match_tok(c, MS_TK_LEFT_BRACE))
        parse_block(c);
    else
        compile_statement(c);
}

static void parse_if_stmt(MsCompiler* c) {
    consume(c, MS_TK_LEFT_PAREN, "Expected '(' after 'if'.");
    expression(c);
    int cond_reg = c->next_reg - 1;
    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after condition.");

    emit(c, ms_enc_ABC(MS_OP_TEST, cond_reg, 0, 0));
    int jmp_then = emit_jmp(c);
    free_reg(c, cond_reg);
    stmt_reg_reset(c);

    parse_body(c);

    int jmp_else = NO_JUMP;
    if (check(c, MS_TK_ELSE)) {
        advance(c);
        jmp_else = emit_jmp(c);
    }
    patch_jmp(c, jmp_then);

    if (jmp_else != NO_JUMP) {
        match_tok(c, MS_TK_NEWLINE);
        if (match_tok(c, MS_TK_IF)) {
            parse_if_stmt(c);
        } else {
            parse_body(c);
        }
        patch_jmp(c, jmp_else);
    }
}

/* ---- while ---- */

static void parse_while_stmt(MsCompiler* c) {
    int loop_start = c->function->chunk.code_count;
    consume(c, MS_TK_LEFT_PAREN, "Expected '(' after 'while'.");
    expression(c);
    int cond_reg = c->next_reg - 1;
    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after condition.");

    emit(c, ms_enc_ABC(MS_OP_TEST, cond_reg, 0, 0));
    int jmp_exit = emit_jmp(c);
    free_reg(c, cond_reg);
    stmt_reg_reset(c);

    MsLoopCtx loop;
    loop.start      = loop_start;
    loop.break_list = NO_JUMP;
    loop.depth      = c->scope_depth;
    loop.enclosing  = c->loop;
    c->loop = &loop;

    consume(c, MS_TK_LEFT_BRACE, "Expected '{' after while condition.");
    parse_block(c);

    int offset = loop_start - c->function->chunk.code_count - 1;
    emit(c, ms_enc_AsBx(MS_OP_JMP, 0, offset));
    patch_jmp(c, jmp_exit);
    patch_list(c, loop.break_list, c->function->chunk.code_count);
    c->loop = loop.enclosing;
}

/* ---- for (C-style) ---- */

static void parse_for_stmt(MsCompiler* c) {
    consume(c, MS_TK_LEFT_PAREN, "Expected '(' after 'for'.");
    begin_scope(c);

    if (match_tok(c, MS_TK_VAR)) {
        parse_var_decl(c);
        match_tok(c, MS_TK_NEWLINE);
        match_tok(c, MS_TK_SEMICOLON);
    } else if (!match_tok(c, MS_TK_SEMICOLON)) {
        compile_expression_stmt(c);
    }

    int loop_start = c->function->chunk.code_count;
    int jmp_exit   = NO_JUMP;

    if (!check(c, MS_TK_SEMICOLON)) {
        expression(c);
        int cond_reg = c->next_reg - 1;
        emit(c, ms_enc_ABC(MS_OP_TEST, cond_reg, 0, 0));
        jmp_exit = emit_jmp(c);
        free_reg(c, cond_reg);
        stmt_reg_reset(c);
    }
    consume(c, MS_TK_SEMICOLON, "Expected ';' in for.");

    /* Save post-expr position; skip it initially */
    int post_start = c->function->chunk.code_count;
    int jmp_body   = emit_jmp(c); /* jump over post, execute body first */

    /* Compile post-expression */
    int post_expr_ip = c->function->chunk.code_count;
    if (!check(c, MS_TK_RIGHT_PAREN)) {
        expression(c);
        stmt_reg_reset(c);
    }
    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after for clauses.");

    /* After post: jump back to loop_start */
    int offset_back = loop_start - c->function->chunk.code_count - 1;
    emit(c, ms_enc_AsBx(MS_OP_JMP, 0, offset_back));

    /* Body starts here */
    int body_ip = c->function->chunk.code_count;
    patch_jmp(c, jmp_body);

    MsLoopCtx loop;
    loop.start      = post_expr_ip;
    loop.break_list = NO_JUMP;
    loop.depth      = c->scope_depth;
    loop.enclosing  = c->loop;
    c->loop = &loop;

    consume(c, MS_TK_LEFT_BRACE, "Expected '{' after for clause.");
    parse_block(c);
    MS_UNUSED(body_ip);
    MS_UNUSED(post_start);

    /* After body: jump to post-expression */
    int off_post = post_expr_ip - c->function->chunk.code_count - 1;
    emit(c, ms_enc_AsBx(MS_OP_JMP, 0, off_post));

    if (jmp_exit != NO_JUMP) patch_jmp(c, jmp_exit);
    patch_list(c, loop.break_list, c->function->chunk.code_count);
    c->loop = loop.enclosing;
    end_scope(c);
}

/* ---- break / continue ---- */

static void parse_break_stmt(MsCompiler* c) {
    if (!c->loop) { error_current(c, "'break' outside loop."); return; }
    int prev = c->loop->break_list;
    c->loop->break_list = emit_break_jmp(c, prev);
}

static void parse_continue_stmt(MsCompiler* c) {
    if (!c->loop) { error_current(c, "'continue' outside loop."); return; }
    int offset = c->loop->start - c->function->chunk.code_count - 1;
    emit(c, ms_enc_AsBx(MS_OP_JMP, 0, offset));
}

/* ---- switch / case / default ---- */

static void parse_switch_stmt(MsCompiler* c) {
    consume(c, MS_TK_LEFT_PAREN, "Expected '(' after 'switch'.");
    expression(c);
    int sw_reg = c->next_reg - 1;
    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after switch expr.");
    consume(c, MS_TK_LEFT_BRACE, "Expected '{' after switch.");

    /* Shim loop context so 'break' inside a case body exits the switch,
       not any enclosing loop. 'continue' is not valid inside switch and
       falls through to the outer loop context naturally via enclosing. */
    MsLoopCtx sw_shim;
    sw_shim.start      = NO_JUMP;   /* no continue target */
    sw_shim.break_list = NO_JUMP;
    sw_shim.depth      = c->scope_depth;
    sw_shim.enclosing  = c->loop;
    c->loop = &sw_shim;

    while (!check(c, MS_TK_RIGHT_BRACE) && !check(c, MS_TK_EOF_TOKEN)) {
        if (match_tok(c, MS_TK_NEWLINE) || match_tok(c, MS_TK_SEMICOLON)) continue;
        if (match_tok(c, MS_TK_CASE)) {
            expression(c);
            int val_reg = c->next_reg - 1;
            int cmp_reg = alloc_reg(c);
            emit(c, ms_enc_ABC(MS_OP_EQ, cmp_reg, sw_reg, val_reg));
            emit(c, ms_enc_ABC(MS_OP_TEST, cmp_reg, 0, 0));
            free_reg(c, cmp_reg);
            free_reg(c, val_reg);
            int jmp_next = emit_jmp(c);
            consume(c, MS_TK_COLON, "Expected ':' after case value.");
            while (!check(c, MS_TK_CASE) && !check(c, MS_TK_DEFAULT) &&
                   !check(c, MS_TK_RIGHT_BRACE) && !check(c, MS_TK_EOF_TOKEN)) {
                if (match_tok(c, MS_TK_NEWLINE) || match_tok(c, MS_TK_SEMICOLON)) continue;
                compile_statement(c);
            }
            int prev_exit = sw_shim.break_list;
            sw_shim.break_list = emit_break_jmp(c, prev_exit);
            patch_jmp(c, jmp_next);
        } else if (match_tok(c, MS_TK_DEFAULT)) {
            consume(c, MS_TK_COLON, "Expected ':' after 'default'.");
            while (!check(c, MS_TK_RIGHT_BRACE) && !check(c, MS_TK_EOF_TOKEN)) {
                if (match_tok(c, MS_TK_NEWLINE) || match_tok(c, MS_TK_SEMICOLON)) continue;
                compile_statement(c);
            }
        } else {
            error_current(c, "Expected 'case' or 'default' in switch.");
            break;
        }
    }

    c->loop = sw_shim.enclosing;
    consume(c, MS_TK_RIGHT_BRACE, "Expected '}' after switch body.");
    free_reg(c, sw_reg);
    stmt_reg_reset(c);
    patch_list(c, sw_shim.break_list, c->function->chunk.code_count);
}

/* ---- return ---- */

static void parse_return_stmt(MsCompiler* c) {
    if (check(c, MS_TK_NEWLINE) || check(c, MS_TK_SEMICOLON) ||
        check(c, MS_TK_RIGHT_BRACE) || check(c, MS_TK_EOF_TOKEN)) {
        emit(c, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0));
    } else {
        expression(c);
        int ret_reg = c->next_reg - 1;
        emit(c, ms_enc_ABC(MS_OP_RETURN, ret_reg, 2, 0));
    }
}

/* ---- function declaration ---- */

void compile_function(MsCompiler* outer, const char* fname, int flen) {
    MsCompiler inner;
    ms_scanner_init(&inner.scanner, outer->scanner.source);
    inner.scanner    = outer->scanner;
    inner.current    = outer->current;
    inner.previous   = outer->previous;
    inner.had_error  = false;
    inner.panic_mode = false;
    inner.diags      = outer->diags;
    inner.diag_count = outer->diag_count;
    inner.max_diags  = outer->max_diags;
    inner.vm         = outer->vm;
    inner.function   = ms_obj_function_new(outer->vm);
    inner.enclosing  = outer;
    inner.local_count = 0;
    inner.scope_depth = 1;
    inner.next_reg    = 0;
    inner.max_reg     = 0;
    inner.loop        = NULL;
    inner.klass       = outer->klass;
    ms_table_init(&inner.string_cache);

    if (flen > 0)
        inner.function->name = ms_obj_string_copy(outer->vm, fname, flen);

    consume(&inner, MS_TK_LEFT_PAREN, "Expected '(' after function name.");
    inner.function->arity = 0;
    if (!check(&inner, MS_TK_RIGHT_PAREN)) {
        do {
            consume(&inner, MS_TK_IDENTIFIER, "Expected parameter name.");
            MsToken pname = inner.previous;
            if (inner.local_count >= 256) {
                error_at(&inner, &pname, "Too many parameters.");
                break;
            }
            int slot = inner.next_reg++;
            if (slot > inner.max_reg) inner.max_reg = slot;
            MsLocal* loc = &inner.locals[inner.local_count++];
            loc->name        = pname;
            loc->depth       = 1;
            loc->is_captured = false;
            loc->slot        = slot;
            inner.function->arity++;
            inner.function->min_arity = inner.function->arity;
        } while (match_tok(&inner, MS_TK_COMMA));
    }
    consume(&inner, MS_TK_RIGHT_PAREN, "Expected ')' after parameters.");
    consume(&inner, MS_TK_LEFT_BRACE, "Expected '{' before function body.");

    while (!check(&inner, MS_TK_RIGHT_BRACE) && !check(&inner, MS_TK_EOF_TOKEN)) {
        if (match_tok(&inner, MS_TK_NEWLINE) || match_tok(&inner, MS_TK_SEMICOLON))
            continue;
        compile_statement(&inner);
    }
    consume(&inner, MS_TK_RIGHT_BRACE, "Expected '}' after function body.");

    emit(&inner, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0));
    inner.function->max_stack_size = inner.max_reg;

    /* Propagate scanner state back to outer */
    outer->scanner  = inner.scanner;
    outer->current  = inner.current;
    outer->previous = inner.previous;
    if (inner.had_error) outer->had_error = true;

    ms_table_free(&inner.string_cache);

    MsObjFunction* proto = inner.function;
    int k = add_constant(outer, MS_OBJ_VAL(proto));
    int r = alloc_reg(outer);
    emit(outer, ms_enc_ABx(MS_OP_CLOSURE, r, k));

    /* Emit one EXTRAARG per upvalue: A=is_local, Bx=index */
    for (int i = 0; i < proto->upvalue_count; i++) {
        emit(outer, ms_enc_ABx(MS_OP_EXTRAARG,
                               inner.upvalues[i].is_local ? 1 : 0,
                               inner.upvalues[i].index));
    }
}

static void parse_fun_decl(MsCompiler* c) {
    consume(c, MS_TK_IDENTIFIER, "Expected function name.");
    MsToken name = c->previous;
    int reg_before = c->next_reg;
    compile_function(c, name.start, name.length);
    int fn_reg = c->next_reg - 1;
    if (c->scope_depth == 0) {
        int k = add_string_constant(c, name.start, name.length);
        emit(c, ms_enc_ABx(MS_OP_DEFGLOBAL, fn_reg, k));
        c->next_reg = reg_before;
    } else {
        MsLocal* local = &c->locals[c->local_count++];
        local->name        = name;
        local->depth       = c->scope_depth;
        local->is_captured = false;
        local->slot        = fn_reg;
    }
}

static void compile_statement(MsCompiler* c) {
    if (match_tok(c, MS_TK_VAR)) {
        parse_var_decl(c);
    } else if (match_tok(c, MS_TK_IF)) {
        parse_if_stmt(c);
        return;
    } else if (match_tok(c, MS_TK_WHILE)) {
        parse_while_stmt(c);
        return;
    } else if (match_tok(c, MS_TK_FOR)) {
        parse_for_stmt(c);
        return;
    } else if (match_tok(c, MS_TK_BREAK)) {
        parse_break_stmt(c);
    } else if (match_tok(c, MS_TK_CONTINUE)) {
        parse_continue_stmt(c);
    } else if (match_tok(c, MS_TK_RETURN)) {
        parse_return_stmt(c);
    } else if (match_tok(c, MS_TK_SWITCH)) {
        parse_switch_stmt(c);
        return;
    } else if (match_tok(c, MS_TK_FUN)) {
        parse_fun_decl(c);
        return;
    } else if (match_tok(c, MS_TK_LEFT_BRACE)) {
        parse_block(c);
        return;
    } else {
        compile_expression_stmt(c);
        return;
    }
    match_tok(c, MS_TK_NEWLINE);
    match_tok(c, MS_TK_SEMICOLON);
}

MsObjFunction* ms_compile(MsVM* vm, const char* source, const char* path,
                           MsDiagnostic* diags, int* diag_count, int max_diags) {
    MsCompiler c;
    compiler_init(&c, vm, source, diags, diag_count, max_diags);
    MS_UNUSED(path);

    while (!check(&c, MS_TK_EOF_TOKEN)) {
        if (match_tok(&c, MS_TK_NEWLINE) || match_tok(&c, MS_TK_SEMICOLON))
            continue;
        compile_statement(&c);
    }

    emit(&c, ms_enc_ABC(MS_OP_RETURN, 0, 0, 0));
    c.function->max_stack_size = c.max_reg;
    ms_table_free(&c.string_cache);

    return c.had_error ? NULL : c.function;
}
