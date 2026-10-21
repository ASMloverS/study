// VM 公共面（T011，20 §1/21 §6）：无堆值表示、值栈/帧/全局表、错误槽、trace
#ifndef INCLUDE_MS_VM_VM_H_
#define INCLUDE_MS_VM_VM_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "common/common.h"
#include "compiler/chunk.h"

typedef struct MsObj MsObj;  // 对象头（obj/obj.h，T012）

// 值类别（22 §1 五 tag + T011 过渡 kind；过渡项归宿：STRING→T013、FUNCTION/NATIVE→T018、RANGE_ITER→T017）
typedef enum MsValueKind {
  MS_VAL_NIL,
  MS_VAL_BOOL,
  MS_VAL_I64,
  MS_VAL_F64,
  MS_VAL_OBJ,         // 堆对象指针（T012）
  MS_VAL_STRING,      // 过渡：借用常量池文本（转义原文；解码归 T013）
  MS_VAL_FUNCTION,    // 过渡：函数 chunk 引用（MsObjFunction 归 T018）
  MS_VAL_NATIVE,      // 过渡：C 内置函数（MsObjNative 归 T018）
  MS_VAL_RANGE_ITER,  // 过渡：range 惰性迭代器（MsObjIter 归 T017）
} MsValueKind;

typedef struct MsVM MsVM;
typedef struct MsValue MsValue;
typedef MsValue (*MsNativeFn)(MsVM* vm, int32_t argc, const MsValue* args);

typedef struct MsValue {
  MsValueKind kind;
  union {
    bool b;
    ms_i64 i;
    double f;
    MsObj* obj;
    struct {
      const char* bytes;  // 借用（常量池/源缓冲）
      int32_t len;
    } str;
    const Chunk* func;
    MsNativeFn native;
    struct {
      ms_i64 cur, stop, step;
    } range;
  } as;
} MsValue;

// 判型内联族（22 §1；28 §1 bool 谓词命名）
static inline bool msIsNil(const MsValue* v) { return v->kind == MS_VAL_NIL; }
static inline bool msIsBool(const MsValue* v) { return v->kind == MS_VAL_BOOL; }
static inline bool msIsI64(const MsValue* v) { return v->kind == MS_VAL_I64; }
static inline bool msIsF64(const MsValue* v) { return v->kind == MS_VAL_F64; }
static inline bool msIsObj(const MsValue* v) { return v->kind == MS_VAL_OBJ; }

// 全局表条目：简单数组 + 名字线性索引（T015 换 map）
typedef struct MsGlobalVar {
  const char* name;  // 借用常量池字符串
  int32_t len;
  MsValue value;
} MsGlobalVar;

// 调用帧（本任务仅模块帧；T018 起函数帧切换）
typedef struct MsFrame {
  const Chunk* chunk;
  int32_t ip;
  int32_t base;  // 帧值栈基
} MsFrame;

struct GcHeap;  // gc/gc.h；msVmInit 创建、msVmFree 销毁

struct MsVM {
  MsValue* stack;
  int32_t stackTop, stackCap;
  MsFrame* frames;
  int32_t frameCount, frameCap;
  MsGlobalVar* globals;
  int32_t globalCount, globalCap;
  struct GcHeap* heap;
  FILE* out;    // print 输出（默认 stdout；单测重定向 tmpfile）
  FILE* trace;  // --trace 逐步日志（NULL 关闭；20 §6）
  bool hasError;  // 运行时错误槽（28 §6：不 longjmp，主循环统一 unwind）
  char errMsg[240];
  int32_t errLine;
  int32_t safepointBudget;  // CHECK_SAFEPOINT 预算递减（抢占归 T029）
};

void msVmInit(MsVM* vm);
void msVmFree(MsVM* vm);
// 执行模块 chunk 至 RETURN；错误置槽后由调用方打印 + 退出码 1（27 §10）
void msVmRun(MsVM* vm, const Chunk* chunk);

// builtins_bridge.c：LOAD_GLOBAL 未命中用户全局时的内置回查
bool msBuiltinLookup(const char* name, int32_t len, MsValue* out);

// ---- value.c：值操作助手 ----
MsValue msValueNil(void);
MsValue msValueBool(bool b);
MsValue msValueInt(ms_i64 v);
MsValue msValueFloat(double v);
MsValue msValueObj(MsObj* obj);
bool msValueIsTruthy(const MsValue* v);  // 仅 false/nil 为假（02 §2）
bool msValueEqual(const MsValue* a, const MsValue* b);  // == 语义（int/float 数值互比）
bool msValueOrder(const MsValue* a, const MsValue* b, int32_t* outCmp);  // false = 不可比较
const char* msValueTypeName(const MsValue* v);
// print str 形态（nil/bool/int/float；string 由 print 直写不经此函数）
void msValueToStr(const MsValue* v, char* buf, size_t cap);
void msFormatFloat(char* buf, size_t cap, double v);  // 最短往返 + ".0" 保形
MsValue msValueFromConst(const ConstEntry* e, bool* okOut);  // BIGINT 置 *okOut=false

#endif  // INCLUDE_MS_VM_VM_H_
