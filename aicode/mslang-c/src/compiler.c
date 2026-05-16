#include "compiler_impl.h"
#include "ms/table.h"
#include "ms/opcode.h"
#include "ms/optimize.h"
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
    c->loop          = NULL;
    c->klass         = NULL;
    c->in_async_fun  = false;
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

/* ---- throw ---- */

static void parse_throw_stmt(MsCompiler* c) {
    expression(c);
    int reg = c->next_reg - 1;
    emit(c, ms_enc_ABC(MS_OP_THROW, reg, 0, 0));
    free_reg(c, reg);
}

/* ---- try / catch ---- */

static void parse_try_stmt(MsCompiler* c) {
    /* Allocate a register for the caught exception value.
       We keep it fixed so TRY can encode it, and it becomes the
       catch-variable local after the catch block starts. */
    int catch_reg = alloc_reg(c);

    /* Emit TRY with a placeholder offset; A = catch_reg */
    int try_ip = emit(c, ms_enc_AsBx(MS_OP_TRY, catch_reg, 0));

    /* Compile try body */
    match_tok(c, MS_TK_NEWLINE);
    consume(c, MS_TK_LEFT_BRACE, "Expected '{' after 'try'.");
    parse_block(c);

    /* ENDTRY pops the handler */
    emit(c, ms_enc_ABC(MS_OP_ENDTRY, 0, 0, 0));

    /* JMP past catch block */
    int jmp_past = emit_jmp(c);

    /* Patch TRY offset to point here (catch block start) */
    int catch_start = c->function->chunk.code_count;
    {
        int offset = catch_start - try_ip - 1;
        c->function->chunk.code[try_ip] = ms_enc_AsBx(MS_OP_TRY, catch_reg, offset);
    }

    /* catch (e) { body } */
    consume(c, MS_TK_CATCH, "Expected 'catch' after try block.");
    consume(c, MS_TK_LEFT_PAREN, "Expected '(' after 'catch'.");
    consume(c, MS_TK_IDENTIFIER, "Expected catch variable name.");
    MsToken catch_var = c->previous;
    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after catch variable.");

    /* Bind catch_var as a local pointing to catch_reg.
       Ensure next_reg is above catch_reg so subsequent alloc_reg
       won't clobber the catch variable's slot. */
    begin_scope(c);
    if (c->next_reg <= catch_reg) c->next_reg = catch_reg + 1;
    if (c->local_count < 256) {
        MsLocal* loc = &c->locals[c->local_count++];
        loc->name        = catch_var;
        loc->depth       = c->scope_depth;
        loc->is_captured = false;
        loc->slot        = catch_reg;
    }

    match_tok(c, MS_TK_NEWLINE);
    consume(c, MS_TK_LEFT_BRACE, "Expected '{' after catch clause.");
    /* parse_block will call end_scope and free locals */
    while (!check(c, MS_TK_RIGHT_BRACE) && !check(c, MS_TK_EOF_TOKEN)) {
        if (match_tok(c, MS_TK_NEWLINE) || match_tok(c, MS_TK_SEMICOLON))
            continue;
        compile_statement(c);
    }
    consume(c, MS_TK_RIGHT_BRACE, "Expected '}' after catch block.");
    end_scope(c);

    patch_jmp(c, jmp_past);
    free_reg(c, catch_reg);
}

/* ---- import ---- */

/*
 * import "path"               -- IMPORT A Bx  (K(Bx)=path, R(A)=module, stored as global 'basename')
 * from "path" import name     -- IMPORT A Bx + IMPFROM A A C  (K(C)=name, store as global 'name')
 * from "path" import name as alias -- same + IMPALIAS (store alias)
 */
static void parse_import_stmt(MsCompiler* c) {
    /* import "path" */
    consume(c, MS_TK_STRING, "Expected path string after 'import'.");
    int path_k = add_string_constant(c, c->previous.start + 1,
                                     c->previous.length - 2);
    int mod_reg = alloc_reg(c);
    emit(c, ms_enc_ABx(MS_OP_IMPORT, mod_reg, path_k));

    /* Derive module name from path (last component, strip .ms) */
    const char* path = c->previous.start + 1;
    int path_len = c->previous.length - 2;
    int de = -1;
    for (int i = 0; i < path_len; i++) {
        if (path[i] == '/' || path[i] == '\\') de = i;
    }
    const char* base = path + de + 1;
    int base_len = path_len - de - 1;
    /* Strip .ms */
    if (base_len > 3 && base[base_len - 3] == '.' &&
        base[base_len - 2] == 'm' && base[base_len - 1] == 's') {
        base_len -= 3;
    }

    /* Define module as global under its basename */
    if (c->scope_depth == 0) {
        int name_k = add_string_constant(c, base, base_len);
        emit(c, ms_enc_ABx(MS_OP_DEFGLOBAL, mod_reg, name_k));
        free_reg(c, mod_reg);
    } else {
        /* Local scope: just keep it as a local */
        MsToken fake_tok;
        fake_tok.type   = MS_TK_IDENTIFIER;
        fake_tok.start  = base;
        fake_tok.length = base_len;
        fake_tok.line   = c->previous.line;
        fake_tok.column = c->previous.column;
        MsLocal* loc = &c->locals[c->local_count++];
        loc->name        = fake_tok;
        loc->depth       = c->scope_depth;
        loc->is_captured = false;
        loc->slot        = mod_reg;
    }
}

static void parse_from_import_stmt(MsCompiler* c) {
    /* from "path" import name [as alias] */
    consume(c, MS_TK_STRING, "Expected path string after 'from'.");
    int path_k = add_string_constant(c, c->previous.start + 1,
                                     c->previous.length - 2);
    consume(c, MS_TK_IMPORT, "Expected 'import' after path.");
    consume(c, MS_TK_IDENTIFIER, "Expected name after 'import'.");
    MsToken name_tok = c->previous;
    int name_k = add_string_constant(c, name_tok.start, name_tok.length);

    /* IMPORT: load module into a temp reg */
    int mod_reg = alloc_reg(c);
    emit(c, ms_enc_ABx(MS_OP_IMPORT, mod_reg, path_k));

    /* IMPFROM: extract the named export */
    int val_reg = alloc_reg(c);
    emit(c, ms_enc_ABC(MS_OP_IMPFROM, val_reg, mod_reg, name_k));
    free_reg(c, mod_reg);

    /* Determine the binding name (alias or original) */
    MsToken bind_tok = name_tok;
    if (match_tok(c, MS_TK_AS)) {
        consume(c, MS_TK_IDENTIFIER, "Expected alias name after 'as'.");
        bind_tok = c->previous;
    }

    /* Bind */
    if (c->scope_depth == 0) {
        int bind_k = add_string_constant(c, bind_tok.start, bind_tok.length);
        emit(c, ms_enc_ABx(MS_OP_DEFGLOBAL, val_reg, bind_k));
        free_reg(c, val_reg);
    } else {
        MsLocal* loc = &c->locals[c->local_count++];
        loc->name        = bind_tok;
        loc->depth       = c->scope_depth;
        loc->is_captured = false;
        loc->slot        = val_reg;
    }
}

/* ---- defer ---- */

static void parse_defer_stmt(MsCompiler* c) {
    /* expect an anonymous function expression */
    if (!check(c, MS_TK_FUN)) {
        error_current(c, "Expected 'fun' after 'defer'.");
        return;
    }
    advance(c); /* consume 'fun' */
    compile_function(c, NULL, 0);
    int fn_reg = c->next_reg - 1;
    emit(c, ms_enc_ABC(MS_OP_DEFER, fn_reg, 0, 0));
    free_reg(c, fn_reg);
}

/* ---- yield ---- */

static void parse_yield_stmt(MsCompiler* c) {
    if (c->in_async_fun) {
        error_current(c, "'yield' inside async function.");
        return;
    }
    if (check(c, MS_TK_NEWLINE) || check(c, MS_TK_SEMICOLON) ||
        check(c, MS_TK_RIGHT_BRACE) || check(c, MS_TK_EOF_TOKEN)) {
        /* yield with no value: yields nil, returns sent value */
        int dst = alloc_reg(c);
        emit(c, ms_enc_ABC(MS_OP_YIELD, dst, 1, 0));
        free_reg(c, dst);
    } else {
        expression(c);
        int val_reg = c->next_reg - 1;
        /* dst = same slot: yield val, resume sends value back into dst */
        emit(c, ms_enc_ABC(MS_OP_YIELD, val_reg, 2, 0));
        /* val_reg now holds the sent value after resume */
    }
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
    inner.klass        = outer->klass;
    inner.is_generator = false;
    inner.in_async_fun = false;
    ms_table_init(&inner.string_cache);

    if (flen > 0)
        inner.function->name = ms_obj_string_copy(outer->vm, fname, flen);

    /* If compiling a method, reserve slot 0 for 'this' */
    if (outer->klass != NULL) {
        static const char kThis[] = "this";
        MsToken this_tok;
        this_tok.type   = MS_TK_THIS;
        this_tok.start  = kThis;
        this_tok.length = 4;
        this_tok.line   = 0;
        this_tok.column = 0;
        int slot = inner.next_reg++;
        if (slot > inner.max_reg) inner.max_reg = slot;
        MsLocal* loc = &inner.locals[inner.local_count++];
        loc->name        = this_tok;
        loc->depth       = 1;
        loc->is_captured = false;
        loc->slot        = slot;
    }

    inner.function->arity = 0;
    /* Getter syntax has no parens: `get name { body }` */
    if (match_tok(&inner, MS_TK_LEFT_PAREN)) {
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
    }
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

void compile_generator_function(MsCompiler* outer, const char* fname, int flen) {
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
    inner.klass        = outer->klass;
    inner.is_generator = true;
    inner.in_async_fun = false;
    ms_table_init(&inner.string_cache);

    inner.function->is_generator = true;
    if (flen > 0)
        inner.function->name = ms_obj_string_copy(outer->vm, fname, flen);

    inner.function->arity = 0;
    if (match_tok(&inner, MS_TK_LEFT_PAREN)) {
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
    }
    consume(&inner, MS_TK_LEFT_BRACE, "Expected '{' before function body.");

    while (!check(&inner, MS_TK_RIGHT_BRACE) && !check(&inner, MS_TK_EOF_TOKEN)) {
        if (match_tok(&inner, MS_TK_NEWLINE) || match_tok(&inner, MS_TK_SEMICOLON))
            continue;
        compile_statement(&inner);
    }
    consume(&inner, MS_TK_RIGHT_BRACE, "Expected '}' after function body.");

    emit(&inner, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0));
    inner.function->max_stack_size = inner.max_reg;

    outer->scanner  = inner.scanner;
    outer->current  = inner.current;
    outer->previous = inner.previous;
    if (inner.had_error) outer->had_error = true;

    ms_table_free(&inner.string_cache);

    MsObjFunction* proto = inner.function;
    int k = add_constant(outer, MS_OBJ_VAL(proto));
    int r = alloc_reg(outer);
    emit(outer, ms_enc_ABx(MS_OP_CLOSURE, r, k));

    for (int i = 0; i < proto->upvalue_count; i++) {
        emit(outer, ms_enc_ABx(MS_OP_EXTRAARG,
                               inner.upvalues[i].is_local ? 1 : 0,
                               inner.upvalues[i].index));
    }
}

void compile_async_function(MsCompiler* outer, const char* fname, int flen) {
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
    inner.is_generator = false;
    inner.in_async_fun = true;
    ms_table_init(&inner.string_cache);

    inner.function->is_async = true;
    if (flen > 0)
        inner.function->name = ms_obj_string_copy(outer->vm, fname, flen);

    inner.function->arity = 0;
    if (match_tok(&inner, MS_TK_LEFT_PAREN)) {
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
    }
    consume(&inner, MS_TK_LEFT_BRACE, "Expected '{' before function body.");

    while (!check(&inner, MS_TK_RIGHT_BRACE) && !check(&inner, MS_TK_EOF_TOKEN)) {
        if (match_tok(&inner, MS_TK_NEWLINE) || match_tok(&inner, MS_TK_SEMICOLON))
            continue;
        compile_statement(&inner);
    }
    consume(&inner, MS_TK_RIGHT_BRACE, "Expected '}' after function body.");

    emit(&inner, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0));
    inner.function->max_stack_size = inner.max_reg;

    outer->scanner  = inner.scanner;
    outer->current  = inner.current;
    outer->previous = inner.previous;
    if (inner.had_error) outer->had_error = true;

    ms_table_free(&inner.string_cache);

    MsObjFunction* proto = inner.function;
    int k = add_constant(outer, MS_OBJ_VAL(proto));
    int r = alloc_reg(outer);
    emit(outer, ms_enc_ABx(MS_OP_CLOSURE, r, k));

    for (int i = 0; i < proto->upvalue_count; i++) {
        emit(outer, ms_enc_ABx(MS_OP_EXTRAARG,
                               inner.upvalues[i].is_local ? 1 : 0,
                               inner.upvalues[i].index));
    }
}

/* parse_fun_decl: called after TK_FUN is consumed.
   is_async: true if 'async' keyword preceded 'fun'. */
static void parse_fun_decl_impl(MsCompiler* c, bool is_async) {
    bool is_gen = !is_async && match_tok(c, MS_TK_STAR);
    consume(c, MS_TK_IDENTIFIER, "Expected function name.");
    MsToken name = c->previous;
    int reg_before = c->next_reg;
    if (is_async)
        compile_async_function(c, name.start, name.length);
    else if (is_gen)
        compile_generator_function(c, name.start, name.length);
    else
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

/* ---- class declaration ---- */

/* helper: push synthetic local named 'super' at current scope */
static void push_super_local(MsCompiler* c, int class_reg) {
    if (c->local_count >= 256) return;
    /* super local lives in the same slot as class_reg (just an alias) */
    MsToken super_tok;
    super_tok.type   = MS_TK_SUPER;
    super_tok.start  = "super";
    super_tok.length = 5;
    super_tok.line   = 0;
    super_tok.column = 0;
    MsLocal* loc = &c->locals[c->local_count++];
    loc->name        = super_tok;
    loc->depth       = c->scope_depth;
    loc->is_captured = false;
    loc->slot        = class_reg;
}

static void parse_class_decl(MsCompiler* c) {
    consume(c, MS_TK_IDENTIFIER, "Expected class name.");
    MsToken class_name = c->previous;
    int name_k = add_string_constant(c, class_name.start, class_name.length);

    int class_reg = alloc_reg(c);
    emit(c, ms_enc_ABx(MS_OP_CLASS, class_reg, name_k));

    if (c->scope_depth == 0) {
        emit(c, ms_enc_ABx(MS_OP_DEFGLOBAL, class_reg, name_k));
    } else {
        if (c->local_count >= 256) { error_at(c, &class_name, "Too many locals."); return; }
        MsLocal* loc = &c->locals[c->local_count++];
        loc->name        = class_name;
        loc->depth       = c->scope_depth;
        loc->is_captured = false;
        loc->slot        = class_reg;
    }

    MsClassCompiler klass_ctx;
    klass_ctx.enclosing       = c->klass;
    klass_ctx.has_superclass  = false;
    c->klass = &klass_ctx;

    /* Inheritance: class Sub : Super */
    if (match_tok(c, MS_TK_COLON)) {
        consume(c, MS_TK_IDENTIFIER, "Expected superclass name.");
        MsToken super_name = c->previous;
        /* Load superclass value */
        int super_reg = alloc_reg(c);
        int super_local = -1;
        for (int i = c->local_count - 1; i >= 0; i--) {
            MsLocal* l = &c->locals[i];
            if (l->name.length == super_name.length &&
                memcmp(l->name.start, super_name.start, (size_t)super_name.length) == 0) {
                super_local = l->slot; break;
            }
        }
        if (super_local >= 0) {
            emit(c, ms_enc_ABC(MS_OP_MOVE, super_reg, super_local, 0));
        } else {
            int sk = add_string_constant(c, super_name.start, super_name.length);
            emit(c, ms_enc_ABx(MS_OP_GETGLOBAL, super_reg, sk));
        }
        emit(c, ms_enc_ABC(MS_OP_INHERIT, class_reg, super_reg, 0));
        klass_ctx.has_superclass = true;
        /* Store super as local so methods can capture it */
        push_super_local(c, super_reg);
    }

    match_tok(c, MS_TK_NEWLINE);
    consume(c, MS_TK_LEFT_BRACE, "Expected '{' before class body.");

    while (!check(c, MS_TK_RIGHT_BRACE) && !check(c, MS_TK_EOF_TOKEN)) {
        if (match_tok(c, MS_TK_NEWLINE) || match_tok(c, MS_TK_SEMICOLON)) continue;

        if (match_tok(c, MS_TK_STATIC)) {
            /* static method */
            consume(c, MS_TK_IDENTIFIER, "Expected static method name.");
            MsToken meth_name = c->previous;
            int meth_name_k = add_string_constant(c, meth_name.start, meth_name.length);
            compile_function(c, meth_name.start, meth_name.length);
            int closure_reg = c->next_reg - 1;
            emit(c, ms_enc_ABC(MS_OP_STATICMETH, class_reg, closure_reg, meth_name_k));
            free_reg(c, closure_reg);
            continue;
        }

        /* Contextual keywords: get, set, abstract
           Only treated as getter/setter if followed by IDENTIFIER (not '(') */
        if (check(c, MS_TK_IDENTIFIER)) {
            const char* w = c->current.start;
            int wl = c->current.length;
            bool is_get      = (wl == 3 && memcmp(w, "get", 3) == 0);
            bool is_set      = (wl == 3 && memcmp(w, "set", 3) == 0);
            bool is_abstract = (wl == 8 && memcmp(w, "abstract", 8) == 0);

            /* Peek: scan one more token to decide */
            bool followed_by_ident = false;
            if (is_get || is_set || is_abstract) {
                MsScannerState saved = ms_scanner_save(&c->scanner);
                MsToken peek = ms_scanner_next(&c->scanner);
                followed_by_ident = (peek.type == MS_TK_IDENTIFIER);
                ms_scanner_restore(&c->scanner, saved);
            }

            if ((is_get || is_set) && followed_by_ident) {
                advance(c); /* consume 'get' or 'set' */
                bool is_setter = is_set;
                consume(c, MS_TK_IDENTIFIER, "Expected getter/setter name.");
                MsToken meth_name = c->previous;
                int meth_name_k = add_string_constant(c, meth_name.start, meth_name.length);
                compile_function(c, meth_name.start, meth_name.length);
                int closure_reg = c->next_reg - 1;
                int op = is_setter ? MS_OP_SETTER : MS_OP_GETTER;
                emit(c, ms_enc_ABC(op, class_reg, closure_reg, meth_name_k));
                free_reg(c, closure_reg);
                continue;
            }
            if (is_abstract && followed_by_ident) {
                advance(c); /* consume 'abstract' */
                consume(c, MS_TK_IDENTIFIER, "Expected abstract method name.");
                MsToken meth_name = c->previous;
                int meth_name_k = add_string_constant(c, meth_name.start, meth_name.length);
                /* consume optional () */
                if (match_tok(c, MS_TK_LEFT_PAREN)) {
                    while (!check(c, MS_TK_RIGHT_PAREN) && !check(c, MS_TK_EOF_TOKEN))
                        advance(c);
                    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after abstract params.");
                }
                emit(c, ms_enc_ABC(MS_OP_ABSTMETH, class_reg, 0, meth_name_k));
                continue;
            }
        }

        consume(c, MS_TK_IDENTIFIER, "Expected method name.");
        MsToken meth_name = c->previous;
        int meth_name_k = add_string_constant(c, meth_name.start, meth_name.length);
        compile_function(c, meth_name.start, meth_name.length);
        int closure_reg = c->next_reg - 1;
        emit(c, ms_enc_ABC(MS_OP_METHOD, class_reg, closure_reg, meth_name_k));
        free_reg(c, closure_reg);
    }
    consume(c, MS_TK_RIGHT_BRACE, "Expected '}' after class body.");

    c->klass = klass_ctx.enclosing;
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
    } else if (match_tok(c, MS_TK_YIELD)) {
        parse_yield_stmt(c);
    } else if (match_tok(c, MS_TK_TRY)) {
        parse_try_stmt(c);
        return;
    } else if (match_tok(c, MS_TK_THROW)) {
        parse_throw_stmt(c);
    } else if (match_tok(c, MS_TK_DEFER)) {
        parse_defer_stmt(c);
    } else if (match_tok(c, MS_TK_IMPORT)) {
        parse_import_stmt(c);
    } else if (match_tok(c, MS_TK_FROM)) {
        parse_from_import_stmt(c);
    } else if (match_tok(c, MS_TK_SWITCH)) {
        parse_switch_stmt(c);
        return;
    } else if (match_tok(c, MS_TK_ASYNC)) {
        consume(c, MS_TK_FUN, "Expected 'fun' after 'async'.");
        parse_fun_decl_impl(c, true);
        return;
    } else if (match_tok(c, MS_TK_FUN)) {
        parse_fun_decl_impl(c, false);
        return;
    } else if (match_tok(c, MS_TK_CLASS)) {
        parse_class_decl(c);
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

    while (!check(&c, MS_TK_EOF_TOKEN)) {
        if (match_tok(&c, MS_TK_NEWLINE) || match_tok(&c, MS_TK_SEMICOLON))
            continue;
        compile_statement(&c);
    }

    emit(&c, ms_enc_ABC(MS_OP_RETURN, 0, 0, 0));
    c.function->max_stack_size = c.max_reg;
    if (path)
        c.function->script_path = ms_obj_string_copy(vm, path, (int)strlen(path));
    ms_table_free(&c.string_cache);

    if (!c.had_error)
        ms_peephole_optimize(&c.function->chunk);

    return c.had_error ? NULL : c.function;
}
