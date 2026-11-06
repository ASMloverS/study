// 字段表基础版实现（T020）：单层 MsObjMap 直查
#include "obj/shape.h"

#include "obj/obj_class.h"
#include "obj/obj_map.h"

bool msShapeFieldGet(const MsObjInstance* inst, const MsObjString* name, MsValue* out) {
  MsValue key = msValueObj((MsObj*)name);
  return msObjMapGet(inst->fields, &key, out) == MS_MAP_OK;
}

bool msShapeFieldSet(MsVM* vm, MsObjInstance* inst, const MsObjString* name, const MsValue* v) {
  MsValue key = msValueObj((MsObj*)name);
  return msObjMapPut(vm, inst->fields, &key, v) == MS_MAP_OK;
}
