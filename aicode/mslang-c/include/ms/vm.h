#pragma once
#include "ms/object.h"
#include "ms/consts.h"
#include "ms/memory.h"
#include <stdint.h>

#define MS_STACK_SIZE (MS_FRAMES_MAX * MS_STACK_MAX)

typedef struct MsCallFrame {
    MsObjClosure*   closure;
    MsInstruction*  ip;
    MsValue*        slots;
    MsObjClosure**  deferred;
    int             deferred_count;
    int             deferred_capacity;
} MsCallFrame;

#define MS_MAX_EXCEPTION_HANDLERS 16

typedef struct {
    MsInstruction*  handler_ip;
    int             frame_index;
    int             catch_reg;
    MsValue*        stack_top;
} MsExceptionHandler;

typedef enum {
    MS_INTERPRET_OK,
    MS_INTERPRET_COMPILE_ERROR,
    MS_INTERPRET_RUNTIME_ERROR,
    MS_INTERPRET_YIELD,  /* internal: coroutine suspended at yield */
} MsInterpretResult;

typedef enum {
    MS_GC_IDLE,
    MS_GC_MARKING,
    MS_GC_SWEEPING,
} MsGcPhase;

#ifdef MSLANG_VM_STATS
typedef struct {
    uint64_t instruction_count;
    uint64_t minor_gc_count;
    uint64_t major_gc_count;
    uint64_t incremental_step_count;
    uint64_t deopt_event_count;
    size_t   bytes_allocated_peak;
    int      peak_frame_count;
    int      live_objects_after_final_gc;
} MsVMStats;
#endif

typedef struct MsVM {
    MsValue         stack[MS_STACK_SIZE];
    MsValue*        stack_top;
    MsValue         call_result;     /* captures return value from ms_vm_call_sync */
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
    MsTable         module_cache;  /* canonical path -> MsObjModule */
    struct MsCompiler* compiler;
    MsObject**      gray_stack;
    int             gray_count;
    int             gray_capacity;
    MsObjectPool    upvalue_pool;
    MsObjectPool    bound_pool;
    MsExceptionHandler exception_handlers[MS_MAX_EXCEPTION_HANDLERS];
    int             exception_count;
    /* Coroutine support: non-NULL when inside a coroutine execution */
    MsObjCoroutine* current_coroutine;
    /* ASCII single-char string cache (indices 0-127) */
    MsObjString*    ascii_cache[128];
    /* Incremental GC state */
    MsGcPhase       gc_phase;
    MsObject*       sweep_cursor;
    MsObject**      sweep_prev;
#ifdef MSLANG_VM_STATS
    MsVMStats       stats;
#endif
} MsVM;

void              ms_vm_init(MsVM* vm);
void              ms_vm_free(MsVM* vm);
MsInterpretResult ms_vm_interpret(MsVM* vm, const char* source, const char* path);
MsInterpretResult ms_vm_run(MsVM* vm);
void              ms_vm_runtime_error(MsVM* vm, const char* fmt, ...);
void              ms_vm_define_native(MsVM* vm, const char* name, MsNativeFn fn, int arity);

/* Call a closure synchronously from C (used by builtins like map/filter).
   argv[0] = arg1, ..., argv[argc-1] = argN. Result stored in *out.
   Returns MS_INTERPRET_OK or MS_INTERPRET_RUNTIME_ERROR. */
MsInterpretResult ms_vm_call_sync(MsVM* vm, MsValue callee,
                                   MsValue* argv, int argc, MsValue* out);

/* Built-in method dispatch for non-instance receivers (string/list/map/tuple).
   Returns true if method was handled; result stored in *out. */
bool ms_builtin_invoke(MsVM* vm, MsValue receiver, MsObjString* method,
                        int argc, MsValue* argv, MsValue* out);

/* Resume a coroutine, passing sent value. Result in *out (yield or return value).
   Returns MS_INTERPRET_OK (coroutine returned), MS_INTERPRET_YIELD (suspended),
   or MS_INTERPRET_RUNTIME_ERROR. */
MsInterpretResult ms_vm_coro_resume(MsVM* vm, MsObjCoroutine* co,
                                     MsValue sent, MsValue* out);

/* Execute the top-level code of a module function, populating mod->exports
   with all top-level globals defined during execution. */
MsInterpretResult ms_vm_execute_module(MsVM* vm, MsObjFunction* fn,
                                        MsObjModule* mod);

#ifdef MSLANG_VM_STATS
void ms_vm_get_stats(const MsVM* vm, MsVMStats* out);
void ms_vm_reset_stats(MsVM* vm);
#endif
