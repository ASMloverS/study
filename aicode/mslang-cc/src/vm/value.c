// 值操作助手（T011）：构造/真值/相等/排序/类型名/str 形态/浮点格式
// T013：string 归 MS_VAL_OBJ（值相等/字典序经 obj_string；字面量经 intern 合一）
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_string.h"

MsValue msValueNil(void) {
  MsValue v = {MS_VAL_NIL, {0}};
  return v;
}

MsValue msValueBool(bool b) {
  MsValue v = {MS_VAL_BOOL, {0}};
  v.as.b = b;
  return v;
}

MsValue msValueInt(ms_i64 i) {
  MsValue v = {MS_VAL_I64, {0}};
  v.as.i = i;
  return v;
}

MsValue msValueFloat(double f) {
  MsValue v = {MS_VAL_F64, {0}};
  v.as.f = f;
  return v;
}

MsValue msValueObj(MsObj* obj) {
  MsValue v = {MS_VAL_OBJ, {0}};
  v.as.obj = obj;
  return v;
}

static bool isNumeric(const MsValue* v) { return v->kind == MS_VAL_I64 || v->kind == MS_VAL_F64; }

bool msValueIsTruthy(const MsValue* v) {
  if (v->kind == MS_VAL_BOOL) {
    return v->as.b;
  }
  return v->kind != MS_VAL_NIL;  // 其余（含 0/""/迭代器）均为真（02 §2）
}

bool msValueEqual(const MsValue* a, const MsValue* b) {
  if (isNumeric(a) && isNumeric(b)) {  // 数值塔：int/float 数值互比；bool 不参与
    if (a->kind == MS_VAL_I64 && b->kind == MS_VAL_I64) {
      return a->as.i == b->as.i;
    }
    double da = a->kind == MS_VAL_I64 ? (double)a->as.i : a->as.f;
    double db = b->kind == MS_VAL_I64 ? (double)b->as.i : b->as.f;
    return da == db;
  }
  switch (a->kind) {
    case MS_VAL_NIL:
      return b->kind == MS_VAL_NIL;
    case MS_VAL_BOOL:
      return b->kind == MS_VAL_BOOL && a->as.b == b->as.b;
    case MS_VAL_FUNCTION:
      return b->kind == MS_VAL_FUNCTION && a->as.func == b->as.func;
    case MS_VAL_NATIVE:
      return b->kind == MS_VAL_NATIVE && a->as.native == b->as.native;
    case MS_VAL_OBJ: {
      if (b->kind != MS_VAL_OBJ) {
        return false;
      }
      if (a->as.obj->type == MS_OBJ_STRING && b->as.obj->type == MS_OBJ_STRING) {
        return msObjStringEqual((const MsObjString*)a->as.obj, (const MsObjString*)b->as.obj);
      }
      return a->as.obj == b->as.obj;  // 其余身份（list 值等价 T014）
    }
    default:
      return false;  // 迭代器等仅同一性
  }
}

bool msValueOrder(const MsValue* a, const MsValue* b, int32_t* outCmp) {
  if (!(isNumeric(a) && isNumeric(b))) {
    if (a->kind == MS_VAL_OBJ && b->kind == MS_VAL_OBJ && a->as.obj->type == MS_OBJ_STRING &&
        b->as.obj->type == MS_OBJ_STRING) {  // string 字典序（22 §3）
      *outCmp = msObjStringCompare((const MsObjString*)a->as.obj, (const MsObjString*)b->as.obj);
      return true;
    }
    return false;  // 其余组合不可比较
  }
  if (a->kind == MS_VAL_I64 && b->kind == MS_VAL_I64) {
    *outCmp = a->as.i < b->as.i ? -1 : (a->as.i > b->as.i ? 1 : 0);
    return true;
  }
  double da = a->kind == MS_VAL_I64 ? (double)a->as.i : a->as.f;
  double db = b->kind == MS_VAL_I64 ? (double)b->as.i : b->as.f;
  *outCmp = da < db ? -1 : (da > db ? 1 : 0);
  return true;
}

const char* msValueTypeName(const MsValue* v) {
  switch (v->kind) {
    case MS_VAL_NIL:
      return "nil";
    case MS_VAL_BOOL:
      return "bool";
    case MS_VAL_I64:
      return "int";
    case MS_VAL_F64:
      return "float";
    case MS_VAL_FUNCTION:
    case MS_VAL_NATIVE:
      return "function";
    case MS_VAL_OBJ:
      switch (v->as.obj->type) {
        case MS_OBJ_STRING:
          return "string";
        case MS_OBJ_LIST:  // T013 D6 最小桥；其余容器类型名随各容器任务接入
          return "list";
        default:
          return "object";
      }
    case MS_VAL_RANGE_ITER:
      return "iterator";
    case MS_VAL_SLICE:
      return "slice";
  }
  return "unknown";
}

void msFormatFloat(char* buf, size_t cap, double v) {
  if (isnan(v)) {
    snprintf(buf, cap, "nan");
    return;
  }
  if (isinf(v)) {
    snprintf(buf, cap, v < 0 ? "-inf" : "inf");
    return;
  }
  char tmp[40];
  int prec = 17;
  for (int p = 1; p <= 17; p++) {  // 最短往返位数
    snprintf(tmp, sizeof(tmp), "%.*g", p, v);
    if (strtod(tmp, NULL) == v) {
      prec = p;
      break;
    }
  }
  snprintf(tmp, sizeof(tmp), "%.*g", prec, v);
  const char* e = strchr(tmp, 'e');
  if (e != NULL) {  // 温和量级展开为定点（Python 形态：仅 exp < -4 或 ≥ 16 用科学计数）
    int exp10 = atoi(e + 1);
    if (exp10 >= -4 && exp10 < 16) {
      int digitsAfter = prec - 1 - exp10;
      if (digitsAfter < 0) {
        digitsAfter = 0;
      }
      snprintf(buf, cap, "%.*f", digitsAfter, v);
      if (strchr(buf, '.') == NULL) {
        size_t len = strlen(buf);
        snprintf(buf + len, cap - len, ".0");
      }
      return;
    }
  }
  snprintf(buf, cap, "%s", tmp);
  if (strpbrk(buf, ".eE") == NULL) {  // 整值浮点保 ".0"
    size_t len = strlen(buf);
    snprintf(buf + len, cap - len, ".0");
  }
}

void msValueToStr(const MsValue* v, char* buf, size_t cap) {
  switch (v->kind) {
    case MS_VAL_NIL:
      snprintf(buf, cap, "nil");
      break;
    case MS_VAL_BOOL:
      snprintf(buf, cap, v->as.b ? "true" : "false");
      break;
    case MS_VAL_I64:
      snprintf(buf, cap, "%lld", (long long)v->as.i);
      break;
    case MS_VAL_F64:
      msFormatFloat(buf, cap, v->as.f);
      break;
    case MS_VAL_FUNCTION:
      snprintf(buf, cap, "<func %.*s>", (int)v->as.func->nameLen, v->as.func->name);
      break;
    case MS_VAL_NATIVE:
      snprintf(buf, cap, "<native fn>");
      break;
    case MS_VAL_OBJ:  // string 由 print 直写不经此（str 形态归 Task 11）
      snprintf(buf, cap, "<object>");
      break;
    case MS_VAL_RANGE_ITER:
      snprintf(buf, cap, "<range-iter>");
      break;
    case MS_VAL_SLICE:
      snprintf(buf, cap, "<slice>");
      break;
  }
}

MsValue msValueFromConst(MsVM* vm, const ConstEntry* e, bool* okOut) {
  *okOut = true;
  switch (e->kind) {
    case CONST_NIL:
      return msValueNil();
    case CONST_BOOL:
      return msValueBool(e->as.b);
    case CONST_INT:
      return msValueInt(e->as.i);
    case CONST_FLOAT:
      return msValueFloat(e->as.f);
    case CONST_RUNE:
      return msValueInt((ms_i64)e->as.cp);  // rune 即 int 码点（02 §1）
    case CONST_STRING:
    case CONST_RAWSTRING: {
      if (e->strVm == vm && e->strObj != NULL) {
        return msValueObj(e->strObj);  // 缓存命中（intern 产物，永久代存活）
      }
      // 解码/拷贝源均为常量池文本（非 GC 管理）：New 内 guard 收集无窗口
      MsObjString* s = NULL;
      if (e->kind == CONST_STRING) {
        s = msObjStringFromEscaped(vm, e->as.text.start, e->as.text.len);
      } else {
        s = msObjStringNew(vm, e->as.text.start, e->as.text.len);
      }
      if (s == NULL) {
        *okOut = false;
        return msValueNil();
      }
      // 消费式 intern（C1）：miss 零分配入表——未扎根临时串无回收窗口；hit 则 s 弃用归垃圾
      MsObjString* interned = msInternObj(vm, s);
      // 缓存写入去 const（对调用方透明的按 VM 缓存，D3：跨 VM 复跑按 strVm 失效）
      ((ConstEntry*)e)->strObj = &interned->head;
      ((ConstEntry*)e)->strVm = vm;
      return msValueObj(&interned->head);
    }
    case CONST_PROTO: {
      MsValue v = {MS_VAL_FUNCTION, {0}};
      v.as.func = e->as.proto;
      return v;
    }
    case CONST_BIGINT:
      *okOut = false;  // bigint 执行归 T016（占位错误由 VM 报）
      return msValueNil();
    case CONST_INTERP:
      *okOut = false;  // 插值执行归 Task 11（FORMAT_STRING 消费）
      return msValueNil();
  }
  return msValueNil();
}
