// string 方法族（T013，10 §2）：args[0] 恒为接收者（D5）；本任务首批 charLen/compareTo，Task 9 扩全集
#include <stdio.h>
#include <string.h>

#include "obj/obj_string.h"
#include "vm/vm.h"

static MsValue strmCharLen(MsVM* vm, int32_t argc, const MsValue* args) {
  (void)vm;
  (void)argc;
  return msValueInt((ms_i64)((const MsObjString*)args[0].as.obj)->charLen);
}

static MsValue strmCompareTo(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2 || !msIsString(&args[1])) {
    vm->hasError = true;
    snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: compareTo expects a string argument");
    return msValueNil();
  }
  return msValueInt(msObjStringCompare((const MsObjString*)args[0].as.obj,
                                       (const MsObjString*)args[1].as.obj));
}

typedef struct StrMethodEntry {
  const char* name;
  MsNativeFn fn;
} StrMethodEntry;

static const StrMethodEntry kStrMethods[] = {
    {"charLen", strmCharLen},
    {"compareTo", strmCompareTo},
};

bool msStringMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kStrMethods) / sizeof(kStrMethods[0]); i++) {
    const char* n = kStrMethods[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      *out = (MsMethodRef){kStrMethods[i].fn, n, (int32_t)strlen(n)};
      return true;
    }
  }
  return false;
}
