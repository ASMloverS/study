// 过渡版整堆 STW 标记清除 GC（T012，23-gc.md 过渡形态；分代/并发归 T026/T027）
#ifndef INCLUDE_MS_GC_GC_H_
#define INCLUDE_MS_GC_GC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "obj/obj.h"
#include "vm/vm.h"

typedef struct GcHeap {
  MsObj* objects;    // 全堆链表头（头插）
  size_t bytes;      // 在堆字节数（分配量 - 回收量）
  int32_t count;     // 在堆对象数
  size_t threshold;  // 触发全量回收阈值（MS_GC_HEAP_MAX 或默认 8MB）
  MsObj** gray;      // 标记 worklist（容量跨次复用；23-gc §4 灰队列方向）
  int32_t grayCount, grayCap;
  bool collecting;   // gcCollectAll 进行中：嵌套重入守卫
} GcHeap;

typedef void (*MarkChildrenFn)(MsVM* vm, MsObj* obj);
// markChildren 注册扩展点：每个容器任务为自身类型补标记并同步 GC 单测（任务书 T012 §6）
void gcSetMarkFn(MsObjType type, MarkChildrenFn fn);
// 标记入口：root 扫描与已注册 markFn 共用（仅追踪 MS_VAL_OBJ）；仅限收集周期内调用（收集期外置 MARK 不入队会漏扫 children）
// mutator 纪律：C 局部/held MsValue 不是 root——分配新对象前须先把既有引用落栈/全局，防分配-填充窗口期被回收
void gcMarkValue(MsVM* vm, MsValue v);
// 整堆 STW 标记清除（root = 栈 + 全局表）；保留为 runtime.gc() 全量路径（归 T036）
void gcCollectAll(MsVM* vm);
// 分配点 guard：bytes >= threshold → gcCollectAll（msObjAlloc 调用）
void msVmAllocGuard(MsVM* vm);

void gcHeapInit(GcHeap* heap);  // 阈值解析 MS_GC_HEAP_MAX（十进制字节），缺省 8MB
void gcHeapFree(GcHeap* heap);  // 释放全部对象（含 permanent）与 worklist
size_t gcHeapBytes(const MsVM* vm);
int32_t gcHeapObjCount(const MsVM* vm);

#endif  // INCLUDE_MS_GC_GC_H_
