#pragma once
#include "ms/object.h"
#include "ms/table.h"
#include "ms/consts.h"
#include "ms/memory.h"

#define MS_STACK_SIZE (MS_FRAMES_MAX * MS_STACK_MAX)

typedef struct {
    MsObjClosure*   closure;
    MsInstruction*  ip;
    MsValue*        slots;
} MsCallFrame;

typedef enum {
    MS_INTERPRET_OK,
    MS_INTERPRET_COMPILE_ERROR,
    MS_INTERPRET_RUNTIME_ERROR,
} MsInterpretResult;

typedef struct MsVM {
    MsValue         stack[MS_STACK_SIZE];
    MsValue*        stack_top;
    MsCallFrame     frames[MS_FRAMES_MAX];
    int             frame_count;
    MsTable         globals;
    MsTable         strings;
    MsObject*       objects;      /* legacy: unified list (used by major GC) */
    MsObject*       young_objects;
    MsObject*       old_objects;
    MsObject**      remembered_set;
    int             remembered_count;
    int             remembered_capacity;
    size_t          young_bytes;
    size_t          bytes_allocated;
    size_t          next_gc;
    int             minor_count;  /* minor GCs since last major */
    MsObjUpvalue*   open_upvalues;
    MsObjString*    init_string;
    struct MsCompiler* compiler;
    MsObject**      gray_stack;
    int             gray_count;
    int             gray_capacity;
    MsObjectPool    upvalue_pool;
    MsObjectPool    bound_pool;
} MsVM;

void              ms_vm_init(MsVM* vm);
void              ms_vm_free(MsVM* vm);
MsInterpretResult ms_vm_interpret(MsVM* vm, const char* source, const char* path);
MsInterpretResult ms_vm_run(MsVM* vm);
void              ms_vm_runtime_error(MsVM* vm, const char* fmt, ...);
void              ms_vm_define_native(MsVM* vm, const char* name, MsNativeFn fn, int arity);
