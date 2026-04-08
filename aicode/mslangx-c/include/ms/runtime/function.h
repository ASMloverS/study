#ifndef MSLANGC_RUNTIME_FUNCTION_H_
#define MSLANGC_RUNTIME_FUNCTION_H_

#include <stddef.h>
#include <stdint.h>

#include "ms/object.h"
#include "ms/runtime/chunk.h"
#include "ms/string.h"
#include "ms/table.h"
#include "ms/value.h"

struct MsVM;

typedef struct MsCallResult {
  int ok;
  MsValue value;
} MsCallResult;

typedef MsCallResult (*MsNativeFn)(struct MsVM* vm, int argc,
                                   const MsValue* argv);

typedef struct MsFunction {
  MsObject object;
  int arity;
  uint8_t upvalue_count;
  unsigned flags;
  MsString* name;
  MsChunk chunk;
} MsFunction;

typedef struct MsUpvalue {
  MsObject object;
  MsValue* location;
  MsValue closed;
  struct MsUpvalue* next;
} MsUpvalue;

typedef struct MsClosure {
  MsObject object;
  MsFunction* function;
  struct MsModule* module;
  struct MsClass* owner_class;
  MsUpvalue** upvalues;
  uint8_t upvalue_count;
} MsClosure;

typedef struct MsClass {
  MsObject object;
  MsString* name;
  struct MsClass* superclass;
  MsTable methods;
} MsClass;

typedef struct MsInstance {
  MsObject object;
  MsClass* klass;
  MsTable fields;
} MsInstance;

typedef struct MsBoundMethod {
  MsObject object;
  MsValue receiver;
  MsClosure* method;
} MsBoundMethod;

typedef struct MsNativeFunction {
  MsObject object;
  int arity;
  MsString* name;
  MsNativeFn function;
} MsNativeFunction;

MsFunction* ms_function_new(const char* name, size_t length, int arity);
MsClosure* ms_closure_new(MsFunction* function);
MsUpvalue* ms_upvalue_new(MsValue* location);
MsClass* ms_class_new(const char* name, size_t length, MsClass* superclass);
MsInstance* ms_instance_new(MsClass* klass);
MsBoundMethod* ms_bound_method_new(MsValue receiver, MsClosure* method);
MsNativeFunction* ms_native_function_new(const char* name,
                                         size_t length,
                                         int arity,
                                         MsNativeFn function);

void ms_function_free(MsFunction* function);
void ms_closure_free(MsClosure* closure);
void ms_upvalue_free(MsUpvalue* upvalue);
void ms_class_free(MsClass* klass);
void ms_instance_free(MsInstance* instance);
void ms_bound_method_free(MsBoundMethod* bound_method);
void ms_native_function_free(MsNativeFunction* function);

MsString* ms_class_name(const MsClass* klass);
MsClass* ms_class_superclass(const MsClass* klass);
MsTable* ms_class_methods(MsClass* klass);
MsClass* ms_instance_class(const MsInstance* instance);
MsTable* ms_instance_fields(MsInstance* instance);
MsValue ms_bound_method_receiver(const MsBoundMethod* bound_method);
MsClosure* ms_bound_method_method(const MsBoundMethod* bound_method);

MsCallResult ms_call_result_ok(MsValue value);
MsCallResult ms_call_result_error(void);

#endif
