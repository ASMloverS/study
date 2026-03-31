#ifndef MSLANGC_RUNTIME_FUNCTION_H_
#define MSLANGC_RUNTIME_FUNCTION_H_

#include <stddef.h>
#include <stdint.h>

#include "ms/object.h"
#include "ms/runtime/chunk.h"
#include "ms/string.h"
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
  MsUpvalue** upvalues;
  uint8_t upvalue_count;
} MsClosure;

typedef struct MsNativeFunction {
  MsObject object;
  int arity;
  MsString* name;
  MsNativeFn function;
} MsNativeFunction;

MsFunction* ms_function_new(const char* name, size_t length, int arity);
MsClosure* ms_closure_new(MsFunction* function);
MsUpvalue* ms_upvalue_new(MsValue* location);
MsNativeFunction* ms_native_function_new(const char* name,
                                         size_t length,
                                         int arity,
                                         MsNativeFn function);

void ms_function_free(MsFunction* function);
void ms_closure_free(MsClosure* closure);
void ms_upvalue_free(MsUpvalue* upvalue);
void ms_native_function_free(MsNativeFunction* function);

MsCallResult ms_call_result_ok(MsValue value);
MsCallResult ms_call_result_error(void);

#endif