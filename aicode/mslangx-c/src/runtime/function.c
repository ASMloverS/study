#include "ms/runtime/function.h"

#include <stdlib.h>

MsFunction* ms_function_new(const char* name, size_t length, int arity) {
  MsFunction* function = NULL;

  function = (MsFunction*) calloc(1, sizeof(*function));
  if (function == NULL) {
    return NULL;
  }

  ms_object_init(&function->object, MS_OBJ_FUNCTION);
  function->arity = arity;
  function->upvalue_count = 0;
  function->name = NULL;
  ms_chunk_init(&function->chunk);

  if (name != NULL && length > 0) {
    function->name = ms_string_new(name, length);
    if (function->name == NULL) {
      ms_chunk_destroy(&function->chunk);
      free(function);
      return NULL;
    }
  }

  return function;
}

MsClosure* ms_closure_new(MsFunction* function) {
  MsClosure* closure = NULL;

  if (function == NULL) {
    return NULL;
  }

  closure = (MsClosure*) calloc(1, sizeof(*closure));
  if (closure == NULL) {
    return NULL;
  }

  ms_object_init(&closure->object, MS_OBJ_CLOSURE);
  closure->function = function;
  closure->upvalue_count = function->upvalue_count;
  if (closure->upvalue_count > 0) {
    closure->upvalues = (MsUpvalue**) calloc(closure->upvalue_count,
                                             sizeof(*closure->upvalues));
    if (closure->upvalues == NULL) {
      free(closure);
      return NULL;
    }
  }

  return closure;
}

MsUpvalue* ms_upvalue_new(MsValue* location) {
  MsUpvalue* upvalue = NULL;

  upvalue = (MsUpvalue*) calloc(1, sizeof(*upvalue));
  if (upvalue == NULL) {
    return NULL;
  }

  ms_object_init(&upvalue->object, MS_OBJ_UPVALUE);
  upvalue->location = location;
  upvalue->closed = ms_value_nil();
  upvalue->next = NULL;
  return upvalue;
}

MsNativeFunction* ms_native_function_new(const char* name,
                                         size_t length,
                                         int arity,
                                         MsNativeFn function) {
  MsNativeFunction* native_function = NULL;

  if (function == NULL) {
    return NULL;
  }

  native_function = (MsNativeFunction*) calloc(1, sizeof(*native_function));
  if (native_function == NULL) {
    return NULL;
  }

  ms_object_init(&native_function->object, MS_OBJ_NATIVE_FN);
  native_function->arity = arity;
  native_function->function = function;
  if (name != NULL && length > 0) {
    native_function->name = ms_string_new(name, length);
    if (native_function->name == NULL) {
      free(native_function);
      return NULL;
    }
  }

  return native_function;
}

void ms_function_free(MsFunction* function) {
  if (function == NULL) {
    return;
  }

  ms_chunk_destroy(&function->chunk);
  ms_string_free(function->name);
  free(function);
}

void ms_closure_free(MsClosure* closure) {
  if (closure == NULL) {
    return;
  }

  free(closure->upvalues);
  free(closure);
}

void ms_upvalue_free(MsUpvalue* upvalue) {
  free(upvalue);
}

void ms_native_function_free(MsNativeFunction* function) {
  if (function == NULL) {
    return;
  }

  ms_string_free(function->name);
  free(function);
}

MsCallResult ms_call_result_ok(MsValue value) {
  MsCallResult result;

  result.ok = 1;
  result.value = value;
  return result;
}

MsCallResult ms_call_result_error(void) {
  MsCallResult result;

  result.ok = 0;
  result.value = ms_value_nil();
  return result;
}