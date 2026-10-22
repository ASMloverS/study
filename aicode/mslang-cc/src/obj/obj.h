// 对象头与全堆分配器（T012，22 §2/§9）：类型枚举全集 + 头部 + 链表分配；类型行为由 T013+ 各容器任务补
#ifndef INCLUDE_MS_OBJ_OBJ_H_
#define INCLUDE_MS_OBJ_OBJ_H_

#include <stddef.h>

#include "common/common.h"
#include "vm/vm.h"

// 对象类型全集（22 §2 表）；T012 均为 NULL 实现（仅分配挂链，行为归各容器任务）
typedef enum MsObjType {
  MS_OBJ_STRING,
  MS_OBJ_BIGINT,
  MS_OBJ_FUNCTION,
  MS_OBJ_NATIVE,
  MS_OBJ_CLOSURE,
  MS_OBJ_UPVALUE,
  MS_OBJ_CLASS,
  MS_OBJ_INSTANCE,
  MS_OBJ_BOUND_METHOD,
  MS_OBJ_LIST,
  MS_OBJ_MAP,
  MS_OBJ_SET,
  MS_OBJ_TUPLE,
  MS_OBJ_MODULE,
  MS_OBJ_COROUTINE,
  MS_OBJ_CHANNEL,
  MS_OBJ_FUTURE,
  MS_OBJ_ITERATOR,
  MS_OBJ_WEAKREF,
  MS_OBJ_COUNT,
} MsObjType;

#define MS_OBJ_MARK ((ms_u8)(1u << 0))       // 标记清除黑位
#define MS_OBJ_PERMANENT ((ms_u8)(1u << 1))  // 永久代（intern/常量池雏形）：跳过回收；仅保对象自身——持有子引用的永久对象必须同时为 root（T013 intern 表），否则 children 可能被回收

typedef struct MsObj {
  ms_u8 type;          // MsObjType
  ms_u8 flags;         // MS_OBJ_* 位集
  struct MsObj* next;  // 全堆链表（GC 枚举）
  size_t size;         // 过渡：malloc 尺寸（sweep 按 size 递减 bytes；T026 块分配器重整头部）
} MsObj;

// 分配整体 size 字节（含 MsObj 头；调用方传 sizeof(具体对象类型)）；失败返回 NULL
MsObj* msObjAlloc(MsVM* vm, MsObjType type, size_t size);

// 原生方法对象（T013，D5）：实例方法表条目的堆形态（GET_ATTR 物化用；CALL_METHOD 零分配直调不经此）
typedef struct MsObjNative {
  MsObj head;
  MsNativeFn fn;
  const char* name;  // 借用方法表静态串
  int32_t nameLen;
} MsObjNative;

// 绑定方法（T013，D5）：接收者 + 原生方法；OP_CALL bound 分派调用
typedef struct MsObjBoundMethod {
  MsObj head;
  MsValue receiver;
  MsObjNative* method;
} MsObjBoundMethod;

MsObjNative* msObjNativeNew(MsVM* vm, MsNativeFn fn, const char* name, int32_t nameLen);
MsObjBoundMethod* msObjBoundNew(MsVM* vm, MsValue receiver, MsObjNative* method);

#endif  // INCLUDE_MS_OBJ_OBJ_H_
