// VM 公共面（T011，20 §1/21 §6）：无堆值表示、值栈/帧/全局表、错误槽、trace
#ifndef INCLUDE_MS_VM_VM_H_
#define INCLUDE_MS_VM_VM_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "common/common.h"
#include "compiler/chunk.h"

typedef struct MsObj MsObj;  // 对象头（obj/obj.h，T012）

// 值类别（22 §1 五 tag + T011 过渡 kind；过渡项归宿：STRING→T013 已完成、FUNCTION/NATIVE→T018、RANGE_ITER→T017）
typedef enum MsValueKind {
  MS_VAL_NIL,
  MS_VAL_BOOL,
  MS_VAL_I64,
  MS_VAL_F64,
  MS_VAL_OBJ,         // 堆对象指针（T012；string 即 MS_OBJ_STRING）
  MS_VAL_FUNCTION,    // 过渡：函数 chunk 引用（MsObjFunction 归 T018）
  MS_VAL_NATIVE,      // 过渡：C 内置函数（MsObjNative 归 T018）
  MS_VAL_RANGE_ITER,  // 过渡：range 惰性迭代器（MsObjIter 归 T017）
  MS_VAL_SLICE,       // 过渡：切片描述（BUILD_SLICE → GET_INDEX 栈上瞬态；T014/T015 复用）
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
    const Chunk* func;
    MsNativeFn native;
    struct {
      ms_i64 cur, stop, step;
    } range;
    struct {
      ms_i64 start, end, step;
      ms_u8 mask;  // bit0/1/2：start/end/step 显式给出
    } slice;
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
typedef struct MsInternTable MsInternTable;  // obj/obj_string.h；按指针持有避免 vm.h ↔ obj 头环

struct MsVM {
  MsValue* stack;
  int32_t stackTop, stackCap;
  MsFrame* frames;
  int32_t frameCount, frameCap;
  MsGlobalVar* globals;
  int32_t globalCount, globalCap;
  struct GcHeap* heap;
  MsInternTable* intern;  // intern 表（T013；obj/obj_string.h 完整类型，按指针避免头环）
  FILE* out;    // print 输出（默认 stdout；单测重定向 tmpfile）
  FILE* trace;  // --trace 逐步日志（NULL 关闭；20 §6）
  bool hasError;  // 运行时错误槽（28 §6：不 longjmp，主循环统一 unwind）
  char errMsg[240];
  int32_t errLine;
  int32_t safepointBudget;  // CHECK_SAFEPOINT 预算递减（抢占归 T029）
};

void msVmInit(MsVM* vm);
// 释放全部资源后 VM 处于零态（不再隐式重建堆）；复用须重新 msVmInit
void msVmFree(MsVM* vm);
// 执行模块 chunk 至 RETURN；错误置槽后由调用方打印 + 退出码 1（27 §10）
void msVmRun(MsVM* vm, const Chunk* chunk);

// builtins_bridge.c：LOAD_GLOBAL 未命中用户全局时的内置回查
bool msBuiltinLookup(const char* name, int32_t len, MsValue* out);

// str_methods.c：string 实例方法表（T013，10 §2；机制 D5 供容器复用）
typedef struct MsMethodRef {
  MsNativeFn fn;
  const char* name;
  int32_t nameLen;
} MsMethodRef;
bool msStringMethodLookup(const char* name, int32_t len, MsMethodRef* out);  // 未命中 false

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
MsValue msValueFromConst(MsVM* vm, const ConstEntry* e, bool* okOut);  // BIGINT 置 *okOut=false；STRING/RAWSTRING 解码/intern 并按 VM 缓存

#endif  // INCLUDE_MS_VM_VM_H_
