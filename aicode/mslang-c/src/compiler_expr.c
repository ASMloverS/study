#include "compiler_impl.h"
#include "ms/opcode.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* forward declarations */
static void parse_number(MsCompiler* c, bool can_assign);
static void parse_string(MsCompiler* c, bool can_assign);
static void parse_string_interp(MsCompiler* c, bool can_assign);
static void parse_literal(MsCompiler* c, bool can_assign);
static void parse_unary(MsCompiler* c, bool can_assign);
static void parse_binary(MsCompiler* c, bool can_assign);
static void parse_and(MsCompiler* c, bool can_assign);
static void parse_or(MsCompiler* c, bool can_assign);
static void parse_identifier(MsCompiler* c, bool can_assign);
static void parse_call(MsCompiler* c, bool can_assign);
static void parse_dot(MsCompiler* c, bool can_assign);
static void parse_print_stmt(MsCompiler* c, bool can_assign);
static void parse_fun_expr(MsCompiler* c, bool can_assign);
static void parse_yield_expr(MsCompiler* c, bool can_assign);
static void parse_this(MsCompiler* c, bool can_assign);
static void parse_super(MsCompiler* c, bool can_assign);
static void parse_list(MsCompiler* c, bool can_assign);
static void parse_map(MsCompiler* c, bool can_assign);
static void parse_subscript(MsCompiler* c, bool can_assign);

/* get_rule defined after k_rules table at the bottom of this file */
static const MsParseRule* rule_at(MsTokenType t);

/* ---- Pratt core ---- */

MsExprDesc parse_precedence(MsCompiler* c, MsPrecedence min) {
    advance(c);
    MsParseFn prefix = rule_at(c->previous.type)->prefix;
    if (!prefix) {
        error_at(c, &c->previous, "Expected expression.");
        MsExprDesc e; e.kind = EDESC_VOID; e.idx = 0;
        e.true_list = NO_JUMP; e.false_list = NO_JUMP;
        return e;
    }
    bool can_assign = min <= PREC_ASSIGNMENT;
    prefix(c, can_assign);

    while (rule_at(c->current.type)->precedence >= min) {
        advance(c);
        MsParseFn infix = rule_at(c->previous.type)->infix;
        if (infix) infix(c, can_assign);
    }

    /* retrieve the expr from a thread-local via a small trick:
       parse fns push their result into c->last_expr */
    return c->function->chunk.code_count
        ? (MsExprDesc){ EDESC_REG, .idx = c->next_reg - 1,
                        .true_list = NO_JUMP, .false_list = NO_JUMP }
        : (MsExprDesc){ EDESC_VOID, .idx = 0,
                        .true_list = NO_JUMP, .false_list = NO_JUMP };
}

MsExprDesc expression(MsCompiler* c) {
    return parse_precedence(c, PREC_ASSIGNMENT);
}

/* ---- helpers ---- */

static int add_str_const(MsCompiler* c, const char* chars, int len) {
    MsObjString* s = ms_obj_string_copy(c->vm, chars, len);
    return add_constant(c, MS_OBJ_VAL(s));
}

/* ---- prefix parsers ---- */

static void parse_number(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    const char* src = c->previous.start;
    int         len = c->previous.length;
    if (c->previous.type == MS_TK_NUMBER_INT) {
        ms_i64 v;
        if (len > 2 && src[0] == '0' && (src[1]=='x'||src[1]=='X'))
            v = (ms_i64)strtoull(src+2, NULL, 16);
        else if (len > 2 && src[0] == '0' && (src[1]=='b'||src[1]=='B'))
            v = (ms_i64)strtoull(src+2, NULL, 2);
        else if (len > 2 && src[0] == '0' && (src[1]=='o'||src[1]=='O'))
            v = (ms_i64)strtoull(src+2, NULL, 8);
        else
            v = (ms_i64)strtoll(src, NULL, 10);
        int k = add_constant(c, MS_INT_VAL(v));
        int r = alloc_reg(c);
        emit(c, ms_enc_ABx(MS_OP_LOADK, r, k));
    } else {
        double v = strtod(src, NULL);
        int k = add_constant(c, MS_NUMBER_VAL(v));
        int r = alloc_reg(c);
        emit(c, ms_enc_ABx(MS_OP_LOADK, r, k));
    }
}

static int unescape_segment(const char* raw, int raw_len, char* buf, int cap) {
    int out = 0;
    for (int i = 0; i < raw_len && out < cap - 1; i++) {
        if (raw[i] == '\\' && i + 1 < raw_len) {
            i++;
            switch (raw[i]) {
            case 'n':  buf[out++] = '\n'; break;
            case 't':  buf[out++] = '\t'; break;
            case 'r':  buf[out++] = '\r'; break;
            case '0':  buf[out++] = '\0'; break;
            default:   buf[out++] = raw[i]; break;
            }
        } else {
            buf[out++] = raw[i];
        }
    }
    return out;
}

/* acc_reg < 0: allocate fresh register; >= 0: concat segment into it. */
static int load_and_concat(MsCompiler* c, const char* raw, int raw_len, int acc_reg) {
    char buf[4096];
    int out = unescape_segment(raw, raw_len, buf, (int)sizeof(buf));
    if (out == 0 && acc_reg >= 0) return acc_reg;
    int k = add_str_const(c, buf, out);
    int r = alloc_reg(c);
    emit(c, ms_enc_ABx(MS_OP_LOADK, r, k));
    if (acc_reg >= 0) {
        emit(c, ms_enc_ABC(MS_OP_ADD, acc_reg, acc_reg, r));
        free_reg(c, r);
        return acc_reg;
    }
    return r;
}

static void parse_string(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    char buf[4096];
    int out = unescape_segment(c->previous.start + 1, c->previous.length - 2,
                               buf, (int)sizeof(buf));
    int k = add_str_const(c, buf, out);
    int r = alloc_reg(c);
    emit(c, ms_enc_ABx(MS_OP_LOADK, r, k));
}

static void parse_string_interp(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    /* First STRING_INTERP token has a leading '"'; middle continuation segments don't. */
    const char* raw = c->previous.start[0] == '"'
        ? c->previous.start + 1 : c->previous.start;
    int raw_len = c->previous.start[0] == '"'
        ? c->previous.length - 1 : c->previous.length;

    int acc = load_and_concat(c, raw, raw_len, -1);
    if (acc < 0) {
        int k = add_str_const(c, "", 0);
        acc = alloc_reg(c);
        emit(c, ms_enc_ABx(MS_OP_LOADK, acc, k));
    }

    for (;;) {
        parse_precedence(c, PREC_ASSIGNMENT);
        int expr_reg = c->next_reg - 1;
        emit(c, ms_enc_ABC(MS_OP_STR, expr_reg, expr_reg, 0));
        emit(c, ms_enc_ABC(MS_OP_ADD, acc, acc, expr_reg));
        free_reg(c, expr_reg);

        advance(c);
        if (c->previous.type == MS_TK_STRING_INTERP_END) {
            load_and_concat(c, c->previous.start, c->previous.length - 1, acc);
            break;
        }
        load_and_concat(c, c->previous.start, c->previous.length, acc);
    }
}

static void parse_literal(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    int r = alloc_reg(c);
    switch (c->previous.type) {
    case MS_TK_TRUE:  emit(c, ms_enc_ABC(MS_OP_LOADTRUE,  r, 0, 0)); break;
    case MS_TK_FALSE: emit(c, ms_enc_ABC(MS_OP_LOADFALSE, r, 0, 0)); break;
    case MS_TK_NIL:   emit(c, ms_enc_ABC(MS_OP_LOADNIL,   r, 0, 0)); break;
    default: break;
    }
}


static void parse_unary(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    MsTokenType op = c->previous.type;
    parse_precedence(c, PREC_UNARY);
    int src = c->next_reg - 1;
    switch (op) {
    case MS_TK_MINUS:
    case MS_TK_NOT_KW:
    case MS_TK_BANG: {
        MsOpCode opc = (op == MS_TK_MINUS) ? MS_OP_NEG : MS_OP_NOT;
        emit(c, ms_enc_ABC(opc, src, src, 0));
        break;
    }
    case MS_TK_TILDE:
        emit(c, ms_enc_ABC(MS_OP_BNOT, src, src, 0));
        break;
    default: break;
    }
}

/* Attempt constant folding for integer/float arithmetic */
static bool try_fold(MsTokenType op, MsValue lv, MsValue rv, MsValue* out) {
    if (MS_IS_INT(lv) && MS_IS_INT(rv)) {
        ms_i64 a = MS_AS_INT(lv), b = MS_AS_INT(rv);
        switch (op) {
        case MS_TK_PLUS:    *out = MS_INT_VAL(a + b);  return true;
        case MS_TK_MINUS:   *out = MS_INT_VAL(a - b);  return true;
        case MS_TK_STAR:    *out = MS_INT_VAL(a * b);  return true;
        case MS_TK_SLASH:   if (b == 0) return false;
                             *out = MS_NUMBER_VAL((double)a/(double)b); return true;
        case MS_TK_PERCENT: if (b == 0) return false;
                             *out = MS_INT_VAL(a % b);  return true;
        default: return false;
        }
    }
    if (MS_IS_NUMERIC(lv) && MS_IS_NUMERIC(rv)) {
        double a = ms_as_double(lv), b = ms_as_double(rv);
        switch (op) {
        case MS_TK_PLUS:  *out = MS_NUMBER_VAL(a + b); return true;
        case MS_TK_MINUS: *out = MS_NUMBER_VAL(a - b); return true;
        case MS_TK_STAR:  *out = MS_NUMBER_VAL(a * b); return true;
        case MS_TK_SLASH: if (b == 0.0) return false;
                           *out = MS_NUMBER_VAL(a / b); return true;
        default: return false;
        }
    }
    return false;
}

static void parse_binary(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    MsTokenType  op    = c->previous.type;
    MsPrecedence prec  = (MsPrecedence)(rule_at(op)->precedence + 1);
    int          lhs   = c->next_reg - 1;
    int          cc_before = c->function->chunk.code_count;
    int          ck_before = c->function->chunk.constants.count;

    parse_precedence(c, prec);
    int rhs = c->next_reg - 1;

    /* Constant folding: both sides are a single LOADK of numeric const */
    MsChunk* chunk = &c->function->chunk;
    if (rhs == lhs + 1) {
        int li = cc_before - 1, ri = chunk->code_count - 1;
        if (li >= 0 &&
            MS_GET_OP(chunk->code[li]) == MS_OP_LOADK &&
            MS_GET_OP(chunk->code[ri]) == MS_OP_LOADK &&
            MS_GET_A(chunk->code[li]) == lhs &&
            MS_GET_A(chunk->code[ri]) == rhs) {
            int lk = MS_GET_Bx(chunk->code[li]);
            int rk = MS_GET_Bx(chunk->code[ri]);
            MsValue lv = chunk->constants.data[lk];
            MsValue rv = chunk->constants.data[rk];
            MsValue folded;
            if (try_fold(op, lv, rv, &folded)) {
                chunk->code_count    = cc_before - 1;
                chunk->constants.count = ck_before;
                c->next_reg          = lhs;
                int k = add_constant(c, folded);
                int dr = alloc_reg(c);
                emit(c, ms_enc_ABx(MS_OP_LOADK, dr, k));
                return;
            }
        }
    }

    /* RK optimization: if RHS was a single LOADK, use *_RK opcode with direct const index.
       Condition: rhs == lhs+1 and last emitted instr is LOADK targeting rhs. */
    int dr = lhs;
    int ri = chunk->code_count - 1;
    bool rhs_is_k = (rhs == lhs + 1) && (ri >= 0) &&
                    (MS_GET_OP(chunk->code[ri]) == MS_OP_LOADK) &&
                    (MS_GET_A(chunk->code[ri]) == rhs);
    if (rhs_is_k) {
        int k = MS_GET_Bx(chunk->code[ri]);
        chunk->code_count--;  /* remove the LOADK */
        c->next_reg--;        /* free rhs register */
        switch (op) {
        case MS_TK_PLUS:        emit(c, ms_enc_ABC(MS_OP_ADD_RK, dr, lhs, k)); break;
        case MS_TK_MINUS:       emit(c, ms_enc_ABC(MS_OP_SUB_RK, dr, lhs, k)); break;
        case MS_TK_STAR:        emit(c, ms_enc_ABC(MS_OP_MUL_RK, dr, lhs, k)); break;
        case MS_TK_SLASH:       emit(c, ms_enc_ABC(MS_OP_DIV_RK, dr, lhs, k)); break;
        case MS_TK_EQUAL_EQUAL: emit(c, ms_enc_ABC(MS_OP_EQ_RK,  dr, lhs, k)); break;
        case MS_TK_BANG_EQUAL:
            emit(c, ms_enc_ABC(MS_OP_EQ_RK, dr, lhs, k));
            emit(c, ms_enc_ABC(MS_OP_NOT,   dr, dr,  0));
            break;
        case MS_TK_LESS:        emit(c, ms_enc_ABC(MS_OP_LT_RK, dr, lhs, k)); break;
        case MS_TK_LESS_EQUAL:  emit(c, ms_enc_ABC(MS_OP_LE_RK, dr, lhs, k)); break;
        default:
            /* No RK variant for this op; re-emit LOADK and fall through to normal path */
            chunk->code_count++;
            c->next_reg++;
            goto emit_normal;
        }
        c->next_reg = lhs + 1;
        return;
    }

emit_normal:
    /* Normal binary: emit op, result overwrites lhs register */
    switch (op) {
    case MS_TK_PLUS:            emit(c, ms_enc_ABC(MS_OP_ADD,  dr, lhs, rhs)); break;
    case MS_TK_MINUS:           emit(c, ms_enc_ABC(MS_OP_SUB,  dr, lhs, rhs)); break;
    case MS_TK_STAR:            emit(c, ms_enc_ABC(MS_OP_MUL,  dr, lhs, rhs)); break;
    case MS_TK_SLASH:           emit(c, ms_enc_ABC(MS_OP_DIV,  dr, lhs, rhs)); break;
    case MS_TK_PERCENT:         emit(c, ms_enc_ABC(MS_OP_MOD,  dr, lhs, rhs)); break;
    case MS_TK_EQUAL_EQUAL:     emit(c, ms_enc_ABC(MS_OP_EQ,   dr, lhs, rhs)); break;
    case MS_TK_BANG_EQUAL:
        emit(c, ms_enc_ABC(MS_OP_EQ,  dr, lhs, rhs));
        emit(c, ms_enc_ABC(MS_OP_NOT, dr, dr,   0));
        break;
    case MS_TK_LESS:            emit(c, ms_enc_ABC(MS_OP_LT,   dr, lhs, rhs)); break;
    case MS_TK_LESS_EQUAL:      emit(c, ms_enc_ABC(MS_OP_LE,   dr, lhs, rhs)); break;
    case MS_TK_GREATER:         emit(c, ms_enc_ABC(MS_OP_LT,   dr, rhs, lhs)); break;
    case MS_TK_GREATER_EQUAL:   emit(c, ms_enc_ABC(MS_OP_LE,   dr, rhs, lhs)); break;
    case MS_TK_AMPERSAND:       emit(c, ms_enc_ABC(MS_OP_BAND, dr, lhs, rhs)); break;
    case MS_TK_PIPE:            emit(c, ms_enc_ABC(MS_OP_BOR,  dr, lhs, rhs)); break;
    case MS_TK_CARET:           emit(c, ms_enc_ABC(MS_OP_BXOR, dr, lhs, rhs)); break;
    case MS_TK_LESS_LESS:       emit(c, ms_enc_ABC(MS_OP_SHL,  dr, lhs, rhs)); break;
    case MS_TK_GREATER_GREATER: emit(c, ms_enc_ABC(MS_OP_SHR,  dr, lhs, rhs)); break;
    default: break;
    }
    c->next_reg = lhs + 1;
}

/* and: if lhs is falsy, short-circuit to end (result = lhs register).
   TEST A,0 => skip next instr if reg[A] is falsy; JMP jumps over RHS.
   RHS result overwrites lhs register via MOVE so result is always in lhs. */
static void parse_and(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    int lhs = c->next_reg - 1;
    emit(c, ms_enc_ABC(MS_OP_TEST, lhs, 0, 0));
    int jmp = emit_jmp(c);
    parse_precedence(c, PREC_AND);
    int rhs = c->next_reg - 1;
    if (rhs != lhs) {
        emit(c, ms_enc_ABC(MS_OP_MOVE, lhs, rhs, 0));
        c->next_reg = lhs + 1;
    }
    patch_jmp(c, jmp);
}

/* or: if lhs is truthy, short-circuit to end (result = lhs register).
   TEST A,1 => skip next instr if reg[A] is truthy; JMP jumps over RHS. */
static void parse_or(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    int lhs = c->next_reg - 1;
    emit(c, ms_enc_ABC(MS_OP_TEST, lhs, 1, 0));
    int jmp = emit_jmp(c);
    parse_precedence(c, PREC_OR);
    int rhs = c->next_reg - 1;
    if (rhs != lhs) {
        emit(c, ms_enc_ABC(MS_OP_MOVE, lhs, rhs, 0));
        c->next_reg = lhs + 1;
    }
    patch_jmp(c, jmp);
}

/* Resolve local variable: scan backward. Returns slot or -1. */
static int resolve_local(MsCompiler* c, const char* name, int len) {
    for (int i = c->local_count - 1; i >= 0; i--) {
        MsLocal* l = &c->locals[i];
        if (l->name.length == len && memcmp(l->name.start, name, (size_t)len) == 0)
            return l->slot;
    }
    return -1;
}

/* Add an upvalue to this compiler's upvalue table. Returns index. */
static int add_upvalue(MsCompiler* c, bool is_local, int index) {
    int count = c->function->upvalue_count;
    for (int i = 0; i < count; i++) {
        MsUpvalueDesc* uv = &c->upvalues[i];
        if (uv->index == index && uv->is_local == is_local) return i;
    }
    if (count >= MS_MAX_UPVALUES) {
        error_current(c, "Too many upvalues in function.");
        return 0;
    }
    c->upvalues[count].is_local = is_local;
    c->upvalues[count].index    = index;
    return c->function->upvalue_count++;
}

/* Climb enclosing chain to resolve an upvalue. Returns upvalue index or -1. */
int resolve_upvalue(MsCompiler* c, const char* name, int len) {
    if (c->enclosing == NULL) return -1;
    int local = resolve_local(c->enclosing, name, len);
    if (local >= 0) {
        c->enclosing->locals[local].is_captured = true;
        return add_upvalue(c, true, local);
    }
    int uv = resolve_upvalue(c->enclosing, name, len);
    if (uv >= 0) return add_upvalue(c, false, uv);
    return -1;
}

/* Emit a SET for local or global. local_slot >= 0 means local. */
static void emit_assign(MsCompiler* c, int local_slot, int upval_idx,
                        int name_k, int val_reg) {
    if (local_slot >= 0) {
        emit(c, ms_enc_ABC(MS_OP_MOVE, local_slot, val_reg, 0));
        free_reg(c, val_reg);
    } else if (upval_idx >= 0) {
        emit(c, ms_enc_ABx(MS_OP_SETUPVAL, val_reg, upval_idx));
        free_reg(c, val_reg);
    } else {
        emit(c, ms_enc_ABx(MS_OP_SETGLOBAL, val_reg, name_k));
        free_reg(c, val_reg);
    }
}

static void parse_identifier(MsCompiler* c, bool can_assign) {
    const char* name  = c->previous.start;
    int         nlen  = c->previous.length;
    int local_slot    = resolve_local(c, name, nlen);
    int upval_idx     = (local_slot < 0) ? resolve_upvalue(c, name, nlen) : -1;
    int name_k        = (local_slot < 0 && upval_idx < 0)
                        ? add_string_constant(c, name, nlen) : -1;

    /* Compound assignment: read current value first */
    MsTokenType compound = MS_TK_COUNT;
    if (can_assign) {
        if      (check(c, MS_TK_PLUS_EQUAL))    compound = MS_TK_PLUS_EQUAL;
        else if (check(c, MS_TK_MINUS_EQUAL))   compound = MS_TK_MINUS_EQUAL;
        else if (check(c, MS_TK_STAR_EQUAL))    compound = MS_TK_STAR_EQUAL;
        else if (check(c, MS_TK_SLASH_EQUAL))   compound = MS_TK_SLASH_EQUAL;
        else if (check(c, MS_TK_PERCENT_EQUAL)) compound = MS_TK_PERCENT_EQUAL;
    }

    if (can_assign && match_tok(c, MS_TK_EQUAL)) {
        expression(c);
        int val_reg = c->next_reg - 1;
        emit_assign(c, local_slot, upval_idx, name_k, val_reg);
        if (local_slot >= 0) c->next_reg = local_slot + 1;
    } else if (can_assign && compound != MS_TK_COUNT) {
        advance(c);
        int cur = alloc_reg(c);
        if (local_slot >= 0)
            emit(c, ms_enc_ABC(MS_OP_MOVE, cur, local_slot, 0));
        else if (upval_idx >= 0)
            emit(c, ms_enc_ABx(MS_OP_GETUPVAL, cur, upval_idx));
        else
            emit(c, ms_enc_ABx(MS_OP_GETGLOBAL, cur, name_k));
        expression(c);
        int rhs = c->next_reg - 1;
        MsOpCode opc;
        switch (compound) {
        case MS_TK_PLUS_EQUAL:    opc = MS_OP_ADD; break;
        case MS_TK_MINUS_EQUAL:   opc = MS_OP_SUB; break;
        case MS_TK_STAR_EQUAL:    opc = MS_OP_MUL; break;
        case MS_TK_SLASH_EQUAL:   opc = MS_OP_DIV; break;
        default:                  opc = MS_OP_MOD; break;
        }
        emit(c, ms_enc_ABC(opc, cur, cur, rhs));
        free_reg(c, rhs);
        emit_assign(c, local_slot, upval_idx, name_k, cur);
        if (local_slot >= 0) c->next_reg = local_slot + 1;
    } else {
        int r = alloc_reg(c);
        if (local_slot >= 0)
            emit(c, ms_enc_ABC(MS_OP_MOVE, r, local_slot, 0));
        else if (upval_idx >= 0)
            emit(c, ms_enc_ABx(MS_OP_GETUPVAL, r, upval_idx));
        else
            emit(c, ms_enc_ABx(MS_OP_GETGLOBAL, r, name_k));
    }
}

static int parse_args(MsCompiler* c) {
    int argc = 0;
    if (!check(c, MS_TK_RIGHT_PAREN)) {
        do {
            expression(c);
            argc++;
        } while (match_tok(c, MS_TK_COMMA));
    }
    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after arguments.");
    return argc;
}

/* ---- this ---- */

static void parse_this(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    if (!c->klass) {
        error_at(c, &c->previous, "'this' outside class.");
        return;
    }
    int r = alloc_reg(c);
    emit(c, ms_enc_ABC(MS_OP_MOVE, r, 0, 0));
}

/* ---- super ---- */

static void parse_super(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    if (!c->klass || !c->klass->has_superclass) {
        error_at(c, &c->previous, "'super' outside subclass.");
        return;
    }
    consume(c, MS_TK_DOT, "Expected '.' after 'super'.");
    consume(c, MS_TK_IDENTIFIER, "Expected method name after 'super.'.");
    int name_k = add_string_constant(c, c->previous.start, c->previous.length);

    int super_uv = resolve_upvalue(c, "super", 5);
    if (super_uv < 0) {
        error_at(c, &c->previous, "Cannot resolve 'super' upvalue.");
        return;
    }

    /* GETSUPER A=result_reg, B=super_uv, C=name_k
       VM creates BoundMethod(receiver=slot0, method=super.name_k) */
    int result_reg = alloc_reg(c);
    emit(c, ms_enc_ABC(MS_OP_GETSUPER, result_reg, super_uv, name_k));

    if (check(c, MS_TK_LEFT_PAREN)) {
        /* Call the bound method immediately */
        advance(c); /* consume '(' */
        int argc = parse_args(c);
        emit(c, ms_enc_ABC(MS_OP_CALL, result_reg, argc, result_reg + 1));
        c->next_reg = result_reg + 1;
    }
    /* else: result_reg holds BoundMethod for deferred call */
}

/* ---- dot (property access / method call) ---- */

static void parse_dot(MsCompiler* c, bool can_assign) {
    consume(c, MS_TK_IDENTIFIER, "Expected property name after '.'.");
    int name_k = add_string_constant(c, c->previous.start, c->previous.length);
    int obj_reg = c->next_reg - 1;

    if (can_assign && match_tok(c, MS_TK_EQUAL)) {
        /* SETPROP: obj_reg.name_k = rhs */
        expression(c);
        int val_reg = c->next_reg - 1;
        int ic_slot = c->function->ic_count++;
        emit(c, ms_enc_ABC(MS_OP_SETPROP, val_reg, obj_reg, MS_K_TO_RK(name_k)));
        /* EXTRAARG: A=val_reg (RETURN target), Bx=ic_slot */
        emit(c, ms_enc_ABx(MS_OP_EXTRAARG, val_reg, ic_slot));
        emit(c, ms_enc_ABC(MS_OP_MOVE, obj_reg, val_reg, 0));
        c->next_reg = obj_reg + 1;
    } else if (check(c, MS_TK_LEFT_PAREN)) {
        advance(c);
        int argc = parse_args(c);
        int ic_slot = c->function->ic_count++;
        emit(c, ms_enc_ABC(MS_OP_INVOKE, obj_reg, name_k, argc));
        /* EXTRAARG: A=obj_reg (result dst), Bx=ic_slot */
        emit(c, ms_enc_ABx(MS_OP_EXTRAARG, obj_reg, ic_slot));
        c->next_reg = obj_reg + 1;
    } else {
        int ic_slot = c->function->ic_count++;
        emit(c, ms_enc_ABC(MS_OP_GETPROP, obj_reg, obj_reg, MS_K_TO_RK(name_k)));
        /* EXTRAARG: A=obj_reg (RETURN target = destination reg), Bx=ic_slot */
        emit(c, ms_enc_ABx(MS_OP_EXTRAARG, obj_reg, ic_slot));
    }
}

static void parse_call(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    int fn_reg = c->next_reg - 1;
    int first_arg = c->next_reg;
    int argc = parse_args(c);
    emit(c, ms_enc_ABC(MS_OP_CALL, fn_reg, argc, first_arg));
    /* After call, result is in fn_reg; arg registers are freed. */
    c->next_reg = fn_reg + 1;
}

static void parse_fun_expr(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    compile_function(c, NULL, 0);
}

static void parse_yield_expr(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    /* yield [expr] -- usable as expression: result = sent value on resume */
    if (check(c, MS_TK_NEWLINE) || check(c, MS_TK_SEMICOLON) ||
        check(c, MS_TK_RIGHT_BRACE) || check(c, MS_TK_EOF_TOKEN) ||
        check(c, MS_TK_RIGHT_PAREN)) {
        int dst = alloc_reg(c);
        emit(c, ms_enc_ABC(MS_OP_YIELD, dst, 1, 0));
        /* dst now holds the sent value */
    } else {
        expression(c);
        int val_reg = c->next_reg - 1;
        emit(c, ms_enc_ABC(MS_OP_YIELD, val_reg, 2, 0));
        /* val_reg now holds the sent value after resume */
    }
}

/* ---- collection literals ---- */

static void parse_list(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    int dest = alloc_reg(c);  /* destination for NEWLIST result */
    int count = 0;
    if (!check(c, MS_TK_RIGHT_BRACKET)) {
        do {
            expression(c);
            count++;
        } while (match_tok(c, MS_TK_COMMA) && !check(c, MS_TK_RIGHT_BRACKET));
    }
    consume(c, MS_TK_RIGHT_BRACKET, "Expected ']' after list elements.");
    /* elements are in dest+1 .. dest+count; emit NEWLIST A=dest B=count */
    emit(c, ms_enc_ABC(MS_OP_NEWLIST, dest, count, 0));
    c->next_reg = dest + 1;
}

static void parse_map(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    int dest = alloc_reg(c);
    int pairs = 0;
    if (!check(c, MS_TK_RIGHT_BRACE)) {
        do {
            expression(c);  /* key */
            consume(c, MS_TK_COLON, "Expected ':' after map key.");
            expression(c);  /* value */
            pairs++;
        } while (match_tok(c, MS_TK_COMMA) && !check(c, MS_TK_RIGHT_BRACE));
    }
    consume(c, MS_TK_RIGHT_BRACE, "Expected '}' after map entries.");
    emit(c, ms_enc_ABC(MS_OP_NEWMAP, dest, pairs, 0));
    c->next_reg = dest + 1;
}

/* parse_grouping already handles (expr). Override to detect tuple:
   (expr,) or (expr, expr, ...) */
static void parse_tuple_or_grouping(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    /* peek: empty parens () = grouping / no tuple */
    if (check(c, MS_TK_RIGHT_PAREN)) {
        consume(c, MS_TK_RIGHT_PAREN, "Expected ')'.");
        /* emit nil as placeholder */
        int r = alloc_reg(c);
        emit(c, ms_enc_ABC(MS_OP_LOADNIL, r, 0, 0));
        return;
    }
    int dest = alloc_reg(c);
    expression(c);  /* first element, in dest+1 */
    int count = 1;
    bool is_tuple = false;
    while (match_tok(c, MS_TK_COMMA)) {
        is_tuple = true;
        if (check(c, MS_TK_RIGHT_PAREN)) break;  /* trailing comma */
        expression(c);
        count++;
    }
    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after expression.");
    if (is_tuple) {
        emit(c, ms_enc_ABC(MS_OP_NEWTUPLE, dest, count, 0));
        c->next_reg = dest + 1;
    } else {
        /* grouping: result is already in dest+1, move down */
        emit(c, ms_enc_ABC(MS_OP_MOVE, dest, dest + 1, 0));
        c->next_reg = dest + 1;
    }
}

/* ---- subscript (infix: expr[idx]) ---- */

static void parse_subscript(MsCompiler* c, bool can_assign) {
    int obj_reg = c->next_reg - 1;
    expression(c);
    int idx_reg = c->next_reg - 1;
    consume(c, MS_TK_RIGHT_BRACKET, "Expected ']' after index.");

    if (can_assign && match_tok(c, MS_TK_EQUAL)) {
        /* SETIDX A=obj_reg, B=idx_reg, C=val_reg */
        expression(c);
        int val_reg = c->next_reg - 1;
        emit(c, ms_enc_ABC(MS_OP_SETIDX, obj_reg, idx_reg, val_reg));
        /* free idx and val registers; result stays in obj_reg */
        c->next_reg = obj_reg + 1;
    } else {
        /* GETIDX A=obj_reg, B=obj_reg, C=idx_reg */
        emit(c, ms_enc_ABC(MS_OP_GETIDX, obj_reg, obj_reg, idx_reg));
        c->next_reg = obj_reg + 1;
    }
}

/* ---- statement helpers ---- */

static void parse_print_stmt(MsCompiler* c, bool can_assign) {
    MS_UNUSED(can_assign);
    consume(c, MS_TK_LEFT_PAREN, "Expected '(' after 'print'.");
    int print_reg = alloc_reg(c);
    MsObjString* pname = ms_obj_string_copy(c->vm, "print", 5);
    int pk = add_constant(c, MS_OBJ_VAL(pname));
    emit(c, ms_enc_ABx(MS_OP_GETGLOBAL, print_reg, pk));
    int first_arg = c->next_reg;
    int argc = 0;
    if (!check(c, MS_TK_RIGHT_PAREN)) {
        do {
            expression(c);
            argc++;
        } while (match_tok(c, MS_TK_COMMA));
    }
    consume(c, MS_TK_RIGHT_PAREN, "Expected ')' after print args.");
    emit(c, ms_enc_ABC(MS_OP_CALL, print_reg, argc, first_arg));
    stmt_reg_reset(c);
}

void compile_expression_stmt(MsCompiler* c) {
    if (check(c, MS_TK_PRINT)) {
        advance(c);
        parse_print_stmt(c, false);
    } else {
        expression(c);
        stmt_reg_reset(c);
    }
    match_tok(c, MS_TK_NEWLINE);
    match_tok(c, MS_TK_SEMICOLON);
}

/* ---- parse rule table ---- */

#define RULE(pfx, ifx, prec) { pfx, ifx, prec }
#define NO NULL

static const MsParseRule k_rules[MS_TK_COUNT] = {
    /* LEFT_PAREN */      RULE(parse_tuple_or_grouping, parse_call, PREC_CALL),
    /* RIGHT_PAREN */     RULE(NO, NO, PREC_NONE),
    /* LEFT_BRACE */      RULE(parse_map, NO, PREC_NONE),
    /* RIGHT_BRACE */     RULE(NO, NO, PREC_NONE),
    /* LEFT_BRACKET */    RULE(parse_list, parse_subscript, PREC_CALL),
    /* RIGHT_BRACKET */   RULE(NO, NO, PREC_NONE),
    /* COMMA */           RULE(NO, NO, PREC_NONE),
    /* DOT */             RULE(NO, parse_dot, PREC_CALL),
    /* SEMICOLON */       RULE(NO, NO, PREC_NONE),
    /* COLON */           RULE(NO, NO, PREC_NONE),
    /* QUESTION */        RULE(NO, NO, PREC_NONE),
    /* BANG */            RULE(parse_unary, NO, PREC_NONE),
    /* BANG_EQUAL */      RULE(NO, parse_binary, PREC_EQUALITY),
    /* EQUAL */           RULE(NO, NO, PREC_NONE),
    /* EQUAL_EQUAL */     RULE(NO, parse_binary, PREC_EQUALITY),
    /* GREATER */         RULE(NO, parse_binary, PREC_COMPARISON),
    /* GREATER_EQUAL */   RULE(NO, parse_binary, PREC_COMPARISON),
    /* GREATER_GREATER */ RULE(NO, parse_binary, PREC_SHIFT),
    /* LESS */            RULE(NO, parse_binary, PREC_COMPARISON),
    /* LESS_EQUAL */      RULE(NO, parse_binary, PREC_COMPARISON),
    /* LESS_LESS */       RULE(NO, parse_binary, PREC_SHIFT),
    /* PLUS */            RULE(NO, parse_binary, PREC_TERM),
    /* PLUS_EQUAL */      RULE(NO, NO, PREC_NONE),
    /* MINUS */           RULE(parse_unary, parse_binary, PREC_TERM),
    /* MINUS_EQUAL */     RULE(NO, NO, PREC_NONE),
    /* STAR */            RULE(NO, parse_binary, PREC_FACTOR),
    /* STAR_EQUAL */      RULE(NO, NO, PREC_NONE),
    /* SLASH */           RULE(NO, parse_binary, PREC_FACTOR),
    /* SLASH_EQUAL */     RULE(NO, NO, PREC_NONE),
    /* PERCENT */         RULE(NO, parse_binary, PREC_FACTOR),
    /* PERCENT_EQUAL */   RULE(NO, NO, PREC_NONE),
    /* AMPERSAND */       RULE(NO, parse_binary, PREC_BIT_AND),
    /* PIPE */            RULE(NO, parse_binary, PREC_BIT_OR),
    /* CARET */           RULE(NO, parse_binary, PREC_BIT_XOR),
    /* TILDE */           RULE(parse_unary, NO, PREC_NONE),
    /* DOT_DOT */         RULE(NO, NO, PREC_NONE),
    /* IDENTIFIER */      RULE(parse_identifier, NO, PREC_NONE),
    /* STRING */          RULE(parse_string, NO, PREC_NONE),
    /* STRING_INTERP */   RULE(parse_string_interp, NO, PREC_NONE),
    /* STRING_INTERP_END*/RULE(NO, NO, PREC_NONE),
    /* NUMBER_INT */      RULE(parse_number, NO, PREC_NONE),
    /* NUMBER_FLOAT */    RULE(parse_number, NO, PREC_NONE),
    /* AND */             RULE(NO, parse_and, PREC_AND),
    /* OR */              RULE(NO, parse_or,  PREC_OR),
    /* NOT_KW */          RULE(parse_unary, NO, PREC_NONE),
    /* IF */              RULE(NO, NO, PREC_NONE),
    /* ELSE */            RULE(NO, NO, PREC_NONE),
    /* WHILE */           RULE(NO, NO, PREC_NONE),
    /* FOR */             RULE(NO, NO, PREC_NONE),
    /* IN */              RULE(NO, NO, PREC_NONE),
    /* BREAK */           RULE(NO, NO, PREC_NONE),
    /* CONTINUE */        RULE(NO, NO, PREC_NONE),
    /* RETURN */          RULE(NO, NO, PREC_NONE),
    /* VAR */             RULE(NO, NO, PREC_NONE),
    /* FUN */             RULE(parse_fun_expr, NO, PREC_NONE),
    /* CLASS */           RULE(NO, NO, PREC_NONE),
    /* THIS */            RULE(parse_this, NO, PREC_NONE),
    /* SUPER */           RULE(parse_super, NO, PREC_NONE),
    /* STATIC */          RULE(NO, NO, PREC_NONE),
    /* TRUE */            RULE(parse_literal, NO, PREC_NONE),
    /* FALSE */           RULE(parse_literal, NO, PREC_NONE),
    /* NIL */             RULE(parse_literal, NO, PREC_NONE),
    /* PRINT */           RULE(NO, NO, PREC_NONE),
    /* IMPORT */          RULE(NO, NO, PREC_NONE),
    /* FROM */            RULE(NO, NO, PREC_NONE),
    /* AS */              RULE(NO, NO, PREC_NONE),
    /* TRY */             RULE(NO, NO, PREC_NONE),
    /* CATCH */           RULE(NO, NO, PREC_NONE),
    /* THROW */           RULE(NO, NO, PREC_NONE),
    /* DEFER */           RULE(NO, NO, PREC_NONE),
    /* YIELD */           RULE(parse_yield_expr, NO, PREC_NONE),
    /* SWITCH */          RULE(NO, NO, PREC_NONE),
    /* CASE */            RULE(NO, NO, PREC_NONE),
    /* DEFAULT */         RULE(NO, NO, PREC_NONE),
    /* ENUM */            RULE(NO, NO, PREC_NONE),
    /* NEWLINE */         RULE(NO, NO, PREC_NONE),
    /* ERROR */           RULE(NO, NO, PREC_NONE),
    /* EOF_TOKEN */       RULE(NO, NO, PREC_NONE),
};

static const MsParseRule* rule_at(MsTokenType t) { return &k_rules[t]; }
const MsParseRule* get_rule(MsTokenType t)        { return &k_rules[t]; }
