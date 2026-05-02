#include "ms/runtime/function.h"

#include <stdlib.h>

static int ms_function_init_optional_name(const char* name,
                                          size_t length,
                                          MsString** out_name) {
  if (out_name == NULL) {
    return 0;
  }

  *out_name = NULL;
  if (name == NULL || length == 0) {
    return 1;
  }

  *out_name = ms_string_new(name, length);
  return *out_name != NULL;
}

MsFunction* ms_function_new(const char* name, size_t length, int arity) {
  MsFunction* function = NULL;

  function = (MsFunction*) calloc(1, sizeof(*function));
  if (function == NULL) {
    return NULL;
  }

  ms_object_init(&function->object, MS_OBJ_FUNCTION);
  function->arity = arity;
  ms_chunk_init(&function->chunk);

  if (!ms_function_init_optional_name(name, length, &function->name)) {
    ms_chunk_destroy(&function->chunk);
    free(function);
    return NULL;
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

MsClass* ms_class_new(const char* name, size_t length, MsClass* superclass) {
  MsClass* klass = NULL;

  klass = (MsClass*) calloc(1, sizeof(*klass));
  if (klass == NULL) {
    return NULL;
  }

  ms_object_init(&klass->object, MS_OBJ_CLASS);
  klass->superclass = superclass;
  ms_table_init(&klass->methods);
  if (!ms_function_init_optional_name(name, length, &klass->name)) {
    ms_table_destroy(&klass->methods);
    free(klass);
    return NULL;
  }

  return klass;
}

MsInstance* ms_instance_new(MsClass* klass) {
  MsInstance* instance = NULL;

  if (klass == NULL) {
    return NULL;
  }

  instance = (MsInstance*) calloc(1, sizeof(*instance));
  if (instance == NULL) {
    return NULL;
  }

  ms_object_init(&instance->object, MS_OBJ_INSTANCE);
  instance->klass = klass;
  ms_table_init(&instance->fields);
  return instance;
}

MsBoundMethod* ms_bound_method_new(MsValue receiver, MsClosure* method) {
  MsBoundMethod* bound_method = NULL;

  if (method == NULL) {
    return NULL;
  }

  bound_method = (MsBoundMethod*) calloc(1, sizeof(*bound_method));
  if (bound_method == NULL) {
    return NULL;
  }

  ms_object_init(&bound_method->object, MS_OBJ_BOUND_METHOD);
  bound_method->receiver = receiver;
  bound_method->method = method;
  return bound_method;
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
  if (!ms_function_init_optional_name(name, length, &native_function->name)) {
    free(native_function);
    return NULL;
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

void ms_class_free(MsClass* klass) {
  if (klass == NULL) {
    return;
  }

  ms_table_destroy(&klass->methods);
  ms_string_free(klass->name);
  free(klass);
}

void ms_instance_free(MsInstance* instance) {
  if (instance == NULL) {
    return;
  }

  ms_table_destroy(&instance->fields);
  free(instance);
}

void ms_bound_method_free(MsBoundMethod* bound_method) {
  free(bound_method);
}

void ms_native_function_free(MsNativeFunction* function) {
  if (function == NULL) {
    return;
  }

  ms_string_free(function->name);
  free(function);
}

MsString* ms_class_name(const MsClass* klass) {
  if (klass == NULL) {
    return NULL;
  }
  return klass->name;
}

MsClass* ms_class_superclass(const MsClass* klass) {
  if (klass == NULL) {
    return NULL;
  }
  return klass->superclass;
}

MsTable* ms_class_methods(MsClass* klass) {
  if (klass == NULL) {
    return NULL;
  }
  return &klass->methods;
}

MsClass* ms_instance_class(const MsInstance* instance) {
  if (instance == NULL) {
    return NULL;
  }
  return instance->klass;
}

MsTable* ms_instance_fields(MsInstance* instance) {
  if (instance == NULL) {
    return NULL;
  }
  return &instance->fields;
}

MsValue ms_bound_method_receiver(const MsBoundMethod* bound_method) {
  if (bound_method == NULL) {
    return ms_value_nil();
  }
  return bound_method->receiver;
}

MsClosure* ms_bound_method_method(const MsBoundMethod* bound_method) {
  if (bound_method == NULL) {
    return NULL;
  }
  return bound_method->method;
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
