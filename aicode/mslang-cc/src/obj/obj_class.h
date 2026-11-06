// 类对象族（T020，22 §2/06）：CLASS（方法表/MRO/字段默认）与 INSTANCE（字段表）
#ifndef INCLUDE_MS_OBJ_OBJ_CLASS_H_
#define INCLUDE_MS_OBJ_OBJ_CLASS_H_

#include <stdbool.h>

#include "obj/obj.h"
#include "obj/obj_list.h"
#include "obj/obj_map.h"

typedef struct MsObjClass {
  MsObj head;
  MsObjString* name;  // intern 类名
  MsObjList* bases;  // 声明清单（首基类 + mixins；空 = 无基类）
  MsObjList* mro;  // 线性化结果（自身在首位；INHERIT_INIT 前为 NULL）
  MsObjMap* methods;  // intern 名 → 函数值（普通方法）
  MsObjMap* statics;  // intern 名 → 类属性（static var/const 值、static func 函数值）
  MsObjList* fieldNames;  // 声明字段名清单（intern 串）
  MsObjMap* fieldDefaults;  // 声明字段名 → 默认值（var x = e 类创建时求值）
} MsObjClass;

typedef struct MsObjInstance {
  MsObj head;
  MsObjClass* klass;
  MsObjMap* fields;  // 字段表（Shape 基础版；shape.h API）
} MsObjInstance;

// 创建空类（bases/methods/statics/fieldNames/fieldDefaults 均空表；mro = NULL）；name 须 intern（永久代）或调用方已 root；失败 NULL
MsObjClass* msObjClassNew(MsVM* vm, MsObjString* name);
// 创建实例（klass 可 NULL——C 测试直构场景；须调用方已 root：分配窗口内 GC 可达；fields 空表）；失败 NULL
MsObjInstance* msObjInstanceNew(MsVM* vm, MsObjClass* klass);

// children 标记（gcSetMarkFn 注册转发）
void msObjClassMarkChildren(MsVM* vm, MsObj* obj);
void msObjInstanceMarkChildren(MsVM* vm, MsObj* obj);

// MRO 中类成员判定（GET_ATTR/instanceof 共用）：x 的 mro 含 target
bool msClassMroContains(const MsObjClass* x, const MsObjClass* target);

static inline bool msIsClass(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_CLASS;
}
static inline bool msIsInstance(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_INSTANCE;
}

#endif  // INCLUDE_MS_OBJ_OBJ_CLASS_H_
