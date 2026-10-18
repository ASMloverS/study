// AST 规范化文本输出（T009）：节点 + 关键域 + Binder 标注；忽略纯位置差异
// 换行模型：节点不自带尾换行；多行节点的子级由父级前置 '\n'（子级自缩进），内联子级前置空格；
// 闭合 ')' 追加在最后一个元素行尾（经典紧凑 S-expr）；depth < 0 为单行内联模式
#include <stdio.h>
#include <string.h>

#include "parser/ast.h"

static void dumpNode(FILE* out, const AstNode* n, int depth);

static void dumpIndent(FILE* out, int depth) {
  for (int i = 0; i < depth; i++) {
    fputs("  ", out);
  }
}

// 头行：多行模式缩进 + (head；内联模式无缩进
static void dumpHead(FILE* out, int depth, const char* head) {
  if (depth >= 0) {
    dumpIndent(out, depth);
  }
  fprintf(out, "(%s", head);
}

static void dumpNewChild(FILE* out) { fputc('\n', out); }    // 换行子级（子级自缩进）
static void dumpInlineChild(FILE* out) { fputc(' ', out); }  // 同行内联子级
static void dumpEnd(FILE* out) { fputc(')', out); }          // 闭合贴最后一行

// 子级序列：每个换行一行
static void dumpKidLines(FILE* out, int depth, AstNode** kids, int32_t count) {
  for (int32_t i = 0; i < count; i++) {
    dumpNewChild(out);
    dumpNode(out, kids[i], depth + 1);
  }
}

static const char* binOpName(AstBinOp op) {
  switch (op) {
    case AST_OP_ADD:
      return "+";
    case AST_OP_SUB:
      return "-";
    case AST_OP_MUL:
      return "*";
    case AST_OP_DIV:
      return "/";
    case AST_OP_MOD:
      return "%";
    case AST_OP_POW:
      return "**";
    case AST_OP_BIT_AND:
      return "&";
    case AST_OP_BIT_OR:
      return "|";
    case AST_OP_BIT_XOR:
      return "^";
    case AST_OP_SHL:
      return "<<";
    case AST_OP_SHR:
      return ">>";
    case AST_OP_EQ:
      return "==";
    case AST_OP_NE:
      return "!=";
    case AST_OP_LT:
      return "<";
    case AST_OP_LE:
      return "<=";
    case AST_OP_GT:
      return ">";
    case AST_OP_GE:
      return ">=";
    case AST_OP_IS:
      return "is";
    case AST_OP_IN:
      return "in";
    case AST_OP_AND:
      return "&&";
    case AST_OP_OR:
      return "||";
    default:
      return "?";
  }
}

static const char* assignOpName(AstBinOp op) {
  switch (op) {
    case AST_OP_ADD:
      return "+=";
    case AST_OP_SUB:
      return "-=";
    case AST_OP_MUL:
      return "*=";
    case AST_OP_DIV:
      return "/=";
    case AST_OP_MOD:
      return "%=";
    case AST_OP_POW:
      return "**=";
    case AST_OP_BIT_AND:
      return "&=";
    case AST_OP_BIT_OR:
      return "|=";
    case AST_OP_BIT_XOR:
      return "^=";
    case AST_OP_SHL:
      return "<<=";
    case AST_OP_SHR:
      return ">>=";
    default:
      return "=";
  }
}

static void dumpAnn(FILE* out, const AstNode* n) {
  if (n->bindKind == AST_BIND_NONE) {
    return;
  }
  fputs(" [", out);
  if (n->bindKind == AST_BIND_LOCAL) {
    fprintf(out, "local:%d", n->bindSlot);
  } else if (n->bindKind == AST_BIND_GLOBAL) {
    fputs("global", out);
  } else {
    fprintf(out, "upvalue:%d.%d", n->bindUpvalDepth, n->bindSlot);
  }
  if ((n->bindFlags & AST_BIND_FRESH) != 0) {
    fputs(" fresh", out);
  }
  fputc(']', out);
}

static void dumpName(FILE* out, const char* name, int32_t len) { fprintf(out, "%.*s", (int)len, name); }

static void dumpEscaped(FILE* out, const char* s, int32_t len) {
  fputc('"', out);
  for (int32_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '\\':
        fputs("\\\\", out);
        break;
      case '"':
        fputs("\\\"", out);
        break;
      case '\n':
        fputs("\\n", out);
        break;
      case '\t':
        fputs("\\t", out);
        break;
      case '\r':
        fputs("\\r", out);
        break;
      default:
        if (c < 0x20) {
          fprintf(out, "\\x%02x", c);
        } else {
          fputc((char)c, out);  // UTF-8 原样
        }
        break;
    }
  }
  fputc('"', out);
}

static void dumpFloat(FILE* out, double v) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%.17g", v);
  fputs(buf, out);
  if (strpbrk(buf, ".eEin") == NULL) {  // "inf"/"nan" 判定外，补 .0 保持 float 可辨识
    fputs(".0", out);
  }
}

// 参数（单行拼接）：(params (param a) (param b = (int 1)) (param *r))
static void dumpParams(FILE* out, const AstParam* params, int32_t count) {
  fputs("(params", out);
  for (int32_t i = 0; i < count; i++) {
    fputs(" (param ", out);
    if (params[i].rest) {
      fputc('*', out);
    }
    dumpName(out, params[i].name, params[i].nameLen);
    if (params[i].defaultExpr != NULL) {
      fputs(" = ", out);
      dumpNode(out, params[i].defaultExpr, -1);
    }
    fputc(')', out);
  }
  dumpEnd(out);
}

// 推导式 for 子句目标列表（单行）：(x) / (x k)；annotated
static void dumpComprTarget(FILE* out, const AstNode* t) {
  if (t == NULL) {
    fputc('_', out);
  } else if (t->kind == AST_TUPLE) {
    fputc('(', out);
    for (int32_t j = 0; j < t->as.tuple.count; j++) {
      if (j > 0) {
        fputc(' ', out);
      }
      dumpName(out, t->as.tuple.items[j]->as.ident.name, t->as.tuple.items[j]->as.ident.nameLen);
      dumpAnn(out, t->as.tuple.items[j]);
    }
    fputc(')', out);
  } else {
    dumpName(out, t->as.ident.name, t->as.ident.nameLen);
    dumpAnn(out, t);
  }
}

static void dumpNode(FILE* out, const AstNode* n, int depth) {
  if (n == NULL) {
    if (depth >= 0) {
      dumpIndent(out, depth);
    }
    fputs("(_)", out);  // 占位：BAD_TARGET 等已诊断缺省
    return;
  }
  switch (n->kind) {
    case AST_MODULE:
    case AST_BLOCK:
      dumpHead(out, depth, n->kind == AST_MODULE ? "module" : "block");
      dumpKidLines(out, depth, n->as.list.items, n->as.list.count);
      dumpEnd(out);
      break;
    case AST_VAR_DECL: {
      char head[24];
      const char* base = "var";
      if ((n->as.varDecl.flags & AST_VAR_SHORT) != 0) {
        base = "short";
      } else if ((n->as.varDecl.flags & AST_VAR_CONST) != 0) {
        base = "const";
      }
      if ((n->as.varDecl.flags & AST_VAR_STATIC) != 0) {
        snprintf(head, sizeof(head), "static %s", base);
      } else {
        snprintf(head, sizeof(head), "%s", base);
      }
      dumpHead(out, depth, head);
      fputc(' ', out);
      dumpName(out, n->as.varDecl.name, n->as.varDecl.nameLen);
      dumpAnn(out, n);
      if (n->as.varDecl.init != NULL) {
        dumpNewChild(out);
        dumpNode(out, n->as.varDecl.init, depth >= 0 ? depth + 1 : -1);
      }
      dumpEnd(out);
      break;
    }
    case AST_ASSIGN:
      dumpHead(out, depth, "assign");
      fprintf(out, " %s", assignOpName(n->as.assign.op));
      dumpNewChild(out);
      dumpNode(out, n->as.assign.target, depth >= 0 ? depth + 1 : -1);
      dumpNewChild(out);
      dumpNode(out, n->as.assign.value, depth >= 0 ? depth + 1 : -1);
      dumpEnd(out);
      break;
    case AST_DESTRUCTURE: {
      dumpHead(out, depth, "destructure (");
      for (int32_t i = 0; i < n->as.destructure.count; i++) {
        const AstNode* t = n->as.destructure.targets[i];
        if (i > 0) {
          fputc(' ', out);
        }
        if (i == n->as.destructure.restIndex) {
          fputc('*', out);
        }
        if (t == NULL) {
          fputc('_', out);
        } else {
          dumpName(out, t->as.ident.name, t->as.ident.nameLen);
          dumpAnn(out, t);
        }
      }
      fputc(')', out);
      dumpNewChild(out);
      dumpNode(out, n->as.destructure.value, depth >= 0 ? depth + 1 : -1);
      dumpEnd(out);
      break;
    }
    case AST_EXPR_STMT:
      dumpHead(out, depth, "expr");
      dumpNewChild(out);
      dumpNode(out, n->as.exprStmt.expr, depth >= 0 ? depth + 1 : -1);
      dumpEnd(out);
      break;
    case AST_IF:
      dumpHead(out, depth, "if");
      dumpNewChild(out);
      dumpNode(out, n->as.ifStmt.cond, depth >= 0 ? depth + 1 : -1);
      dumpNewChild(out);
      dumpNode(out, n->as.ifStmt.thenBlock, depth >= 0 ? depth + 1 : -1);
      if (n->as.ifStmt.elseNode != NULL) {
        dumpNewChild(out);
        dumpNode(out, n->as.ifStmt.elseNode, depth >= 0 ? depth + 1 : -1);
      }
      dumpEnd(out);
      break;
    case AST_FOR: {
      const char* head = n->as.forStmt.form == AST_FOR_TRI    ? "for-tri"
                         : n->as.forStmt.form == AST_FOR_IN   ? "for-in"
                         : n->as.forStmt.form == AST_FOR_COND ? "for-cond"
                                                              : "for-inf";
      dumpHead(out, depth, head);
      if (n->as.forStmt.form == AST_FOR_IN) {
        fputc(' ', out);
        dumpComprTarget(out, n->as.forStmt.target);
      } else if (n->as.forStmt.form != AST_FOR_INF) {
        const AstNode* parts[3] = {n->as.forStmt.init, n->as.forStmt.cond, n->as.forStmt.post};
        int partsCount = n->as.forStmt.form == AST_FOR_COND ? 1 : 3;
        for (int i = 0; i < partsCount; i++) {
          if (parts[i] == NULL) {
            fputs(" _", out);
          } else {
            dumpInlineChild(out);
            dumpNode(out, parts[i], -1);
          }
        }
      }
      dumpNewChild(out);
      dumpNode(out, n->as.forStmt.body, depth >= 0 ? depth + 1 : -1);
      dumpEnd(out);
      break;
    }
    case AST_WHILE:
      dumpHead(out, depth, "while");
      dumpInlineChild(out);
      dumpNode(out, n->as.whileStmt.cond, -1);
      dumpNewChild(out);
      dumpNode(out, n->as.whileStmt.body, depth >= 0 ? depth + 1 : -1);
      dumpEnd(out);
      break;
    case AST_SWITCH: {
      dumpHead(out, depth, n->as.switchStmt.isType ? "switch-type" : "switch");
      dumpInlineChild(out);
      dumpNode(out, n->as.switchStmt.subject, -1);
      for (int32_t i = 0; i < n->as.switchStmt.count; i++) {
        const AstNode* c = n->as.switchStmt.cases[i];
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        if (c->as.switchCase.isDefault) {
          fputs("(default", out);
        } else {
          fputs("(case", out);
          for (int32_t v = 0; v < c->as.switchCase.valueCount; v++) {
            dumpInlineChild(out);
            dumpNode(out, c->as.switchCase.values[v], -1);
          }
          if (c->as.switchCase.bindName != NULL) {
            fputs(" as ", out);
            dumpName(out, c->as.switchCase.bindName, c->as.switchCase.bindLen);
          }
        }
        if (c->as.switchCase.body != NULL) {
          dumpNewChild(out);
          dumpNode(out, c->as.switchCase.body, depth >= 0 ? depth + 2 : -1);
        }
        dumpEnd(out);
      }
      dumpEnd(out);
      break;
    }
    case AST_BREAK:
      dumpHead(out, depth, "break");
      dumpEnd(out);
      break;
    case AST_CONTINUE:
      dumpHead(out, depth, "continue");
      dumpEnd(out);
      break;
    case AST_RETURN:
    case AST_THROW:
    case AST_YIELD: {
      const char* head = n->kind == AST_RETURN ? "return" : n->kind == AST_THROW ? "throw" : "yield";
      dumpHead(out, depth, head);
      if (n->as.exprStmt.expr != NULL) {
        dumpNewChild(out);
        dumpNode(out, n->as.exprStmt.expr, depth >= 0 ? depth + 1 : -1);
      }
      dumpEnd(out);
      break;
    }
    case AST_DEFER:
      dumpHead(out, depth, "defer");
      dumpNewChild(out);
      dumpNode(out, n->as.exprStmt.expr, depth >= 0 ? depth + 1 : -1);
      dumpEnd(out);
      break;
    case AST_SEND:
      dumpHead(out, depth, "send");
      dumpNewChild(out);
      dumpNode(out, n->as.sendStmt.chan, depth >= 0 ? depth + 1 : -1);
      dumpNewChild(out);
      dumpNode(out, n->as.sendStmt.value, depth >= 0 ? depth + 1 : -1);
      dumpEnd(out);
      break;
    case AST_SELECT: {
      dumpHead(out, depth, "select");
      for (int32_t i = 0; i < n->as.selectStmt.count; i++) {
        const AstNode* c = n->as.selectStmt.cases[i];
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        if (c->as.selectCase.form == AST_SEL_DEFAULT) {
          fputs("(default", out);
        } else {
          fputs(c->as.selectCase.form == AST_SEL_RECV ? "(recv" : "(send", out);
          if (c->as.selectCase.form == AST_SEL_RECV && c->as.selectCase.target != NULL) {
            dumpInlineChild(out);
            dumpNode(out, c->as.selectCase.target, -1);
          }
          dumpInlineChild(out);
          dumpNode(out, c->as.selectCase.chan, -1);
          if (c->as.selectCase.form == AST_SEL_SEND) {
            dumpInlineChild(out);
            dumpNode(out, c->as.selectCase.value, -1);
          }
        }
        if (c->as.selectCase.body != NULL) {
          dumpNewChild(out);
          dumpNode(out, c->as.selectCase.body, depth >= 0 ? depth + 2 : -1);
        }
        dumpEnd(out);
      }
      dumpEnd(out);
      break;
    }
    case AST_IMPORT: {
      dumpHead(out, depth, "import");
      fputc(' ', out);
      dumpEscaped(out, n->as.importStmt.path, n->as.importStmt.pathLen);
      if (n->as.importStmt.alias != NULL) {
        fputs(" as ", out);
        dumpName(out, n->as.importStmt.alias, n->as.importStmt.aliasLen);
      } else if (n->as.importStmt.star) {
        fputs(" *", out);
      } else if (n->as.importStmt.names != NULL) {
        fputs(" {", out);
        for (int32_t i = 0; i < n->as.importStmt.nameCount; i++) {
          const AstImportName* nm = &n->as.importStmt.names[i];
          if (i > 0) {
            fputs(", ", out);
          }
          dumpName(out, nm->name, nm->nameLen);
          if (nm->asName != NULL) {
            fputs(" as ", out);
            dumpName(out, nm->asName, nm->asLen);
          }
        }
        fputs("}", out);
      }
      dumpEnd(out);
      break;
    }
    case AST_FUNC_DECL:
    case AST_FUNC_EXPR: {
      // funcDecl/funcExpr 载荷字段同名，按 kind 取用
      bool isDecl = n->kind == AST_FUNC_DECL;
      const char* name = isDecl ? n->as.funcDecl.name : NULL;
      ms_u8 flags = isDecl ? n->as.funcDecl.flags : n->as.funcExpr.flags;
      dumpHead(out, depth, isDecl ? "func-decl" : "func-expr");
      if (name != NULL) {
        fputc(' ', out);
        dumpName(out, name, n->as.funcDecl.nameLen);
        dumpAnn(out, n);
      }
      if ((flags & AST_FUNC_ASYNC) != 0) {
        fputs(" async", out);
      }
      if ((flags & AST_FUNC_GENERATOR) != 0) {
        fputs(" generator", out);
      }
      if ((flags & AST_FUNC_STATIC) != 0) {
        fputs(" static", out);
      }
      dumpNewChild(out);
      if (depth >= 0) {
        dumpIndent(out, depth + 1);
      }
      dumpParams(out, isDecl ? n->as.funcDecl.params : n->as.funcExpr.params,
                 isDecl ? n->as.funcDecl.paramCount : n->as.funcExpr.paramCount);
      AstNode* body = isDecl ? n->as.funcDecl.body : n->as.funcExpr.body;
      AstNode* exprBody = isDecl ? n->as.funcDecl.exprBody : n->as.funcExpr.exprBody;
      AstNode* tail = body != NULL ? body : exprBody;
      if (tail != NULL) {
        dumpNewChild(out);
        dumpNode(out, tail, depth >= 0 ? depth + 1 : -1);
      }
      dumpEnd(out);
      break;
    }
    case AST_CLASS_DECL: {
      dumpHead(out, depth, n->as.classDecl.isTrait ? "trait-decl" : "class-decl");
      fputc(' ', out);
      dumpName(out, n->as.classDecl.name, n->as.classDecl.nameLen);
      dumpAnn(out, n);
      if (n->as.classDecl.baseCount > 0) {
        fputs(" (bases", out);
        for (int32_t i = 0; i < n->as.classDecl.baseCount; i++) {
          dumpInlineChild(out);
          dumpName(out, n->as.classDecl.bases[i]->as.ident.name, n->as.classDecl.bases[i]->as.ident.nameLen);
        }
        fputc(')', out);
      }
      dumpKidLines(out, depth, n->as.classDecl.members, n->as.classDecl.memberCount);
      dumpEnd(out);
      break;
    }
    case AST_TRY: {
      dumpHead(out, depth, "try");
      dumpNewChild(out);
      dumpNode(out, n->as.tryStmt.tryBlock, depth >= 0 ? depth + 1 : -1);
      for (int32_t i = 0; i < n->as.tryStmt.catchCount; i++) {
        const AstCatchClause* c = &n->as.tryStmt.catches[i];
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        fputs("(catch", out);
        if (c->typeCount > 0) {
          fputs(" (", out);
          for (int32_t t = 0; t < c->typeCount; t++) {
            if (t > 0) {
              fputc(' ', out);
            }
            dumpName(out, c->types[t]->as.ident.name, c->types[t]->as.ident.nameLen);
          }
          fputc(')', out);
        }
        if (c->bindName != NULL) {
          fputc(' ', out);
          dumpName(out, c->bindName, c->bindLen);
        }
        dumpNewChild(out);
        dumpNode(out, c->block, depth >= 0 ? depth + 2 : -1);
        dumpEnd(out);
      }
      if (n->as.tryStmt.finallyBlock != NULL) {
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        fputs("(finally", out);
        dumpNewChild(out);
        dumpNode(out, n->as.tryStmt.finallyBlock, depth >= 0 ? depth + 2 : -1);
        dumpEnd(out);
      }
      dumpEnd(out);
      break;
    }
    // ---------------- 表达式 ----------------
    case AST_IDENT:
      dumpHead(out, depth, "ident");
      fputc(' ', out);
      dumpName(out, n->as.ident.name, n->as.ident.nameLen);
      dumpAnn(out, n);
      dumpEnd(out);
      break;
    case AST_THIS:
      dumpHead(out, depth, "this");
      dumpEnd(out);
      break;
    case AST_NIL:
      dumpHead(out, depth, "nil");
      dumpEnd(out);
      break;
    case AST_BOOL:
      dumpHead(out, depth, "bool");
      fprintf(out, " %s", n->as.boolLit.b ? "true" : "false");
      dumpEnd(out);
      break;
    case AST_INT:
      dumpHead(out, depth, "int");
      fprintf(out, " %lld", (long long)n->as.intLit.v);
      dumpEnd(out);
      break;
    case AST_FLOAT:
      dumpHead(out, depth, "float");
      fputc(' ', out);
      dumpFloat(out, n->as.floatLit.v);
      dumpEnd(out);
      break;
    case AST_BIGINT:
      dumpHead(out, depth, "bigint");
      fputc(' ', out);
      dumpEscaped(out, n->as.bigintLit.start, n->as.bigintLit.len);
      dumpEnd(out);
      break;
    case AST_RUNE:
      dumpHead(out, depth, "rune");
      fprintf(out, " U+%04X", n->as.runeLit.cp);
      dumpEnd(out);
      break;
    case AST_STRING:
      if (n->as.strLit.raw) {
        dumpHead(out, depth, "string raw");
        fputc(' ', out);
        dumpEscaped(out, n->as.strLit.segs[0].start, n->as.strLit.segs[0].len);
        dumpEnd(out);
        break;
      }
      dumpHead(out, depth, "string");
      for (int32_t i = 0; i < n->as.strLit.count; i++) {
        const AstStrSeg* seg = &n->as.strLit.segs[i];
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        if (seg->expr == NULL) {
          fputs("(lit ", out);
          dumpEscaped(out, seg->start, seg->len);
          dumpEnd(out);
        } else {
          fputs("(interp", out);
          dumpNewChild(out);
          dumpNode(out, seg->expr, depth >= 0 ? depth + 2 : -1);
          dumpEnd(out);
        }
      }
      dumpEnd(out);  // 串段恒 ≥1
      break;
    case AST_TUPLE:
    case AST_LIST:
    case AST_SET:
      dumpHead(out, depth, n->kind == AST_TUPLE ? "tuple" : n->kind == AST_LIST ? "list" : "set");
      dumpKidLines(out, depth, n->as.list.items, n->as.list.count);
      dumpEnd(out);
      break;
    case AST_MAP:
      dumpHead(out, depth, "map");
      for (int32_t i = 0; i < n->as.mapLit.count; i++) {
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        fputs("(entry", out);
        dumpInlineChild(out);
        dumpNode(out, n->as.mapLit.keys[i], -1);
        dumpInlineChild(out);
        dumpNode(out, n->as.mapLit.values[i], -1);
        dumpEnd(out);
      }
      dumpEnd(out);
      break;
    case AST_BINARY:
      dumpHead(out, depth, "binary");
      fprintf(out, " %s", binOpName(n->as.binary.op));
      dumpInlineChild(out);
      dumpNode(out, n->as.binary.lhs, -1);
      dumpInlineChild(out);
      dumpNode(out, n->as.binary.rhs, -1);
      dumpEnd(out);
      break;
    case AST_LOGIC:
      dumpHead(out, depth, n->as.binary.op == AST_OP_AND ? "and" : "or");
      dumpInlineChild(out);
      dumpNode(out, n->as.binary.lhs, -1);
      dumpInlineChild(out);
      dumpNode(out, n->as.binary.rhs, -1);
      dumpEnd(out);
      break;
    case AST_UNARY: {
      const char* head = n->as.unary.op == AST_UNOP_NEG ? "neg" : n->as.unary.op == AST_UNOP_NOT ? "not" : "inv";
      dumpHead(out, depth, head);
      dumpInlineChild(out);
      dumpNode(out, n->as.unary.operand, -1);
      dumpEnd(out);
      break;
    }
    case AST_RECV:
    case AST_AWAIT:
    case AST_GO:
      dumpHead(out, depth, n->kind == AST_RECV ? "recv" : n->kind == AST_AWAIT ? "await" : "go");
      dumpInlineChild(out);
      dumpNode(out, n->as.unary.operand, -1);
      dumpEnd(out);
      break;
    case AST_TERNARY:
      dumpHead(out, depth, "ternary");
      dumpInlineChild(out);
      dumpNode(out, n->as.ternary.cond, -1);
      dumpInlineChild(out);
      dumpNode(out, n->as.ternary.thenExpr, -1);
      dumpInlineChild(out);
      dumpNode(out, n->as.ternary.elseExpr, -1);
      dumpEnd(out);
      break;
    case AST_CALL: {
      dumpHead(out, depth, "call");
      dumpNewChild(out);
      dumpNode(out, n->as.call.callee, depth >= 0 ? depth + 1 : -1);
      for (int32_t i = 0; i < n->as.call.count; i++) {
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        if (n->as.call.spread != NULL && n->as.call.spread[i]) {
          fputs("(spread", out);
          dumpNewChild(out);
          dumpNode(out, n->as.call.args[i], depth >= 0 ? depth + 2 : -1);
          dumpEnd(out);
        } else {
          dumpNode(out, n->as.call.args[i], depth + 1);
        }
      }
      dumpEnd(out);
      break;
    }
    case AST_INDEX:
      dumpHead(out, depth, "index");
      dumpInlineChild(out);
      dumpNode(out, n->as.indexExpr.obj, -1);
      dumpInlineChild(out);
      dumpNode(out, n->as.indexExpr.index, -1);
      dumpEnd(out);
      break;
    case AST_MEMBER:
      dumpHead(out, depth, "member");
      dumpInlineChild(out);
      dumpNode(out, n->as.memberExpr.obj, -1);
      fputc(' ', out);
      dumpName(out, n->as.memberExpr.name, n->as.memberExpr.nameLen);
      dumpEnd(out);
      break;
    case AST_SLICE: {
      dumpHead(out, depth, "slice");
      dumpInlineChild(out);
      dumpNode(out, n->as.slice.obj, -1);
      const AstNode* parts[3] = {n->as.slice.start, n->as.slice.end, n->as.slice.step};
      for (int i = 0; i < 3; i++) {
        if (parts[i] == NULL) {
          fputs(" _", out);
        } else {
          dumpInlineChild(out);
          dumpNode(out, parts[i], -1);
        }
      }
      dumpEnd(out);
      break;
    }
    case AST_COMPREHENSION:
    case AST_GEN_EXPR: {
      const char* head = "gen-expr";
      if (n->kind == AST_COMPREHENSION) {
        head = n->as.compr.compKind == AST_COMP_LIST  ? "comp-list"
               : n->as.compr.compKind == AST_COMP_MAP ? "comp-map"
                                                      : "comp-set";
      }
      dumpHead(out, depth, head);
      if (n->kind == AST_COMPREHENSION && n->as.compr.compKind == AST_COMP_MAP && n->as.compr.key != NULL) {
        dumpNewChild(out);
        dumpNode(out, n->as.compr.key, depth >= 0 ? depth + 1 : -1);
      }
      dumpNewChild(out);
      dumpNode(out, n->as.compr.value, depth >= 0 ? depth + 1 : -1);
      for (int32_t i = 0; i < n->as.compr.forCount; i++) {
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        fputs("(for ", out);
        dumpComprTarget(out, n->as.compr.fors[i].target);
        fputc(' ', out);
        dumpNode(out, n->as.compr.fors[i].iter, -1);
        dumpEnd(out);
      }
      for (int32_t i = 0; i < n->as.compr.condCount; i++) {
        dumpNewChild(out);
        if (depth >= 0) {
          dumpIndent(out, depth + 1);
        }
        fputs("(if ", out);
        dumpNode(out, n->as.compr.conds[i], -1);
        dumpEnd(out);
      }
      dumpEnd(out);
      break;
    }
    default:
      dumpHead(out, depth, "?");
      dumpEnd(out);
      break;
  }
}

void astDumpModule(const AstNode* module, FILE* out) {
  dumpNode(out, module, 0);
  fputc('\n', out);
}
