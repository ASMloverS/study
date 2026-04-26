#include <stddef.h>
#include <string.h>

#include "ms/arena.h"
#include "ms/runtime/function.h"
#include "ms/runtime/vm.h"
#include "ms/string.h"

#include "test_assert.h"

static size_t count_tracked_objects(const MsGCState* gc_state) {
  size_t count = 0;
  const MsObject* object = NULL;

  if (gc_state == NULL) {
    return 0;
  }

  object = gc_state->objects;
  while (object != NULL) {
    count += 1;
    object = object->next;
  }

  return count;
}

static MsCallResult test_native_noop(MsVM* vm, int argc, const MsValue* argv) {
  (void) vm;
  (void) argc;
  (void) argv;

  return ms_call_result_ok(ms_value_nil());
}

static int test_gc_tracks_registered_objects(void) {
  MsVM vm;
  MsString* first_string;
  MsString* second_string;
  MsFunction* function;

  ms_vm_init(&vm);

  TEST_ASSERT(vm.gc.objects == NULL);
  TEST_ASSERT(vm.gc.allocation_count == 0);
  TEST_ASSERT(vm.gc.free_count == 0);
  TEST_ASSERT(vm.gc.collection_count == 0);

  first_string = ms_string_from_cstr("alpha");
  second_string = ms_string_from_cstr("beta");
  function = ms_function_new("run", strlen("run"), 0);

  TEST_ASSERT(first_string != NULL);
  TEST_ASSERT(second_string != NULL);
  TEST_ASSERT(function != NULL);

  ms_vm_gc_track_object(&vm, &first_string->object);
  TEST_ASSERT(vm.gc.objects == &first_string->object);
  TEST_ASSERT(first_string->object.marked == 0);
  TEST_ASSERT(first_string->object.next == NULL);
  TEST_ASSERT(vm.gc.allocation_count == 1);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 1);

  ms_vm_gc_track_object(&vm, &function->object);
  TEST_ASSERT(vm.gc.objects == &function->object);
  TEST_ASSERT(function->object.marked == 0);
  TEST_ASSERT(function->object.next == &first_string->object);
  TEST_ASSERT(vm.gc.allocation_count == 2);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 2);

  ms_vm_gc_track_object(&vm, &second_string->object);
  TEST_ASSERT(vm.gc.objects == &second_string->object);
  TEST_ASSERT(second_string->object.marked == 0);
  TEST_ASSERT(second_string->object.next == &function->object);
  TEST_ASSERT(vm.gc.allocation_count == 3);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 3);
  TEST_ASSERT(vm.gc.free_count == 0);
  TEST_ASSERT(vm.gc.collection_count == 0);

  ms_vm_destroy(&vm);
  ms_function_free(function);
  ms_string_free(second_string);
  ms_string_free(first_string);
  return 0;
}

static int test_gc_tracks_native_registration_objects(void) {
  MsVM vm;
  MsModule module;
  MsString* lookup_key;
  MsNativeFunction* native_function = NULL;
  MsValue stored_value = ms_value_nil();
  MsString* tracked_key = NULL;
  int found = 0;
  size_t i;

  ms_vm_init(&vm);
  ms_module_init(&module, "native");

  lookup_key = ms_string_from_cstr("noop");
  TEST_ASSERT(lookup_key != NULL);
  TEST_ASSERT(ms_vm_define_native(&vm, &module, "noop", 0, test_native_noop) != 0);
  TEST_ASSERT(ms_table_count(&module.globals) == 1);
  TEST_ASSERT(ms_table_get(&module.globals, lookup_key, &stored_value, &found));
  TEST_ASSERT(found != 0);
  TEST_ASSERT(ms_value_get_native_function(stored_value, &native_function));
  TEST_ASSERT(native_function != NULL);
  TEST_ASSERT(vm.gc.allocation_count == 2);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 2);
  TEST_ASSERT(vm.gc.objects != NULL);
  TEST_ASSERT(vm.gc.objects->type == MS_OBJ_NATIVE_FN);
  TEST_ASSERT(vm.gc.objects->next != NULL);
  TEST_ASSERT(vm.gc.objects->next->type == MS_OBJ_STRING);
  for (i = 0; i < module.globals.capacity; ++i) {
    if (module.globals.entries[i].key != NULL) {
      tracked_key = module.globals.entries[i].key;
      break;
    }
  }
  TEST_ASSERT(tracked_key != NULL);
  TEST_ASSERT(tracked_key->object.type == MS_OBJ_STRING);

  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_native_function_free(native_function);
  ms_string_free(tracked_key);
  ms_string_free(lookup_key);
  return 0;
}

static int test_compile_time_arena_does_not_touch_gc_state(void) {
  MsVM vm;
  MsArena arena;
  MsString* tracked_string;

  ms_vm_init(&vm);
  ms_arena_init(&arena, 64);

  TEST_ASSERT(ms_arena_alloc(&arena, 32, 8) != NULL);
  TEST_ASSERT(ms_arena_total_bytes(&arena) >= 32);
  TEST_ASSERT(vm.gc.objects == NULL);
  TEST_ASSERT(vm.gc.allocation_count == 0);

  tracked_string = ms_string_from_cstr("arena");
  TEST_ASSERT(tracked_string != NULL);
  ms_vm_gc_track_object(&vm, &tracked_string->object);
  TEST_ASSERT(vm.gc.objects == &tracked_string->object);
  TEST_ASSERT(vm.gc.allocation_count == 1);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 1);

  ms_string_free(tracked_string);
  ms_arena_destroy(&arena);
  ms_vm_destroy(&vm);
  return 0;
}

static int test_temporary_roots_control_gc_lifetime(void) {
  MsVM vm;
  MsString* temporary_string;

  ms_vm_init(&vm);

  temporary_string = ms_string_from_cstr("temporary-root");
  TEST_ASSERT(temporary_string != NULL);
  ms_vm_gc_track_object(&vm, &temporary_string->object);
  TEST_ASSERT(ms_vm_gc_push_temporary_root(&vm, &temporary_string->object));
  TEST_ASSERT(vm.temporary_root_count == 1);

  ms_vm_gc_collect(&vm);
  TEST_ASSERT(vm.gc.collection_count == 1);
  TEST_ASSERT(vm.gc.free_count == 0);
  TEST_ASSERT(vm.gc.objects == &temporary_string->object);
  TEST_ASSERT(vm.temporary_root_count == 1);

  ms_vm_gc_pop_temporary_root(&vm, &temporary_string->object);
  TEST_ASSERT(vm.temporary_root_count == 0);

  ms_vm_gc_collect(&vm);
  TEST_ASSERT(vm.gc.collection_count == 2);
  TEST_ASSERT(vm.gc.free_count == 1);
  TEST_ASSERT(vm.gc.objects == NULL);

  ms_vm_destroy(&vm);
  return 0;
}

static int test_gc_reclaims_unreachable_object_graph(void) {
  MsVM vm;
  MsList* outer_list;
  MsList* inner_list;

  ms_vm_init(&vm);

  outer_list = ms_list_new();
  inner_list = ms_list_new();
  TEST_ASSERT(outer_list != NULL);
  TEST_ASSERT(inner_list != NULL);
  TEST_ASSERT(ms_value_array_append(&outer_list->elements,
                                    ms_value_object((MsObject*) inner_list)));

  ms_vm_gc_track_object(&vm, &outer_list->object);
  ms_vm_gc_track_object(&vm, &inner_list->object);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 2);

  ms_vm_gc_collect(&vm);
  TEST_ASSERT(vm.gc.collection_count == 1);
  TEST_ASSERT(vm.gc.free_count == 2);
  TEST_ASSERT(vm.gc.objects == NULL);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 0);

  ms_vm_destroy(&vm);
  return 0;
}

static int test_gc_keeps_live_object_while_reclaiming_dead_peer(void) {
  MsVM vm;
  MsList* live_outer;
  MsList* live_inner;
  MsList* dead_outer;
  MsList* dead_inner;
  MsObject* rooted_child = NULL;

  ms_vm_init(&vm);

  live_outer = ms_list_new();
  live_inner = ms_list_new();
  dead_outer = ms_list_new();
  dead_inner = ms_list_new();
  TEST_ASSERT(live_outer != NULL);
  TEST_ASSERT(live_inner != NULL);
  TEST_ASSERT(dead_outer != NULL);
  TEST_ASSERT(dead_inner != NULL);
  TEST_ASSERT(ms_value_array_append(&live_outer->elements,
                                    ms_value_object((MsObject*) live_inner)));
  TEST_ASSERT(ms_value_array_append(&dead_outer->elements,
                                    ms_value_object((MsObject*) dead_inner)));

  ms_vm_gc_track_object(&vm, &live_outer->object);
  ms_vm_gc_track_object(&vm, &live_inner->object);
  ms_vm_gc_track_object(&vm, &dead_outer->object);
  ms_vm_gc_track_object(&vm, &dead_inner->object);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 4);
  TEST_ASSERT(ms_vm_gc_push_temporary_root(&vm, &live_outer->object));
  TEST_ASSERT(vm.temporary_root_count == 1);

  ms_vm_gc_collect(&vm);
  TEST_ASSERT(vm.gc.collection_count == 1);
  TEST_ASSERT(vm.gc.free_count == 2);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 2);
  TEST_ASSERT(vm.gc.objects != NULL);
  TEST_ASSERT(live_outer->object.marked == 0);
  TEST_ASSERT(live_inner->object.marked == 0);
  TEST_ASSERT(live_outer->elements.count == 1);
  TEST_ASSERT(live_outer->elements.items[0].type == MS_VAL_OBJECT);
  TEST_ASSERT(ms_value_get_object(live_outer->elements.items[0], &rooted_child));
  TEST_ASSERT(rooted_child == (MsObject*) live_inner);

  ms_vm_gc_pop_temporary_root(&vm, &live_outer->object);
  TEST_ASSERT(vm.temporary_root_count == 0);

  ms_vm_gc_collect(&vm);
  TEST_ASSERT(vm.gc.collection_count == 2);
  TEST_ASSERT(vm.gc.free_count == 4);
  TEST_ASSERT(vm.gc.objects == NULL);

  ms_vm_destroy(&vm);
  return 0;
}

static int test_gc_keeps_module_class_and_native_alive_while_reclaiming_dead_peer(void) {
  MsVM vm;
  MsModule module;
  MsString* class_key;
  MsString* native_lookup_key;
  MsString* tracked_native_key = NULL;
  MsClass* live_class = NULL;
  MsNativeFunction* live_native_function = NULL;
  MsList* dead_outer;
  MsList* dead_inner;
  MsValue stored_value = ms_value_nil();
  int found = 0;
  int inserted_new = 0;
  size_t i;

  ms_vm_init(&vm);
  ms_module_init(&module, "unit");
  ms_vm_set_current_module(&vm, &module);

  class_key = ms_string_from_cstr("alive_class");
  native_lookup_key = ms_string_from_cstr("alive_native");
  live_class = ms_class_new("AliveClass", strlen("AliveClass"), NULL);
  TEST_ASSERT(class_key != NULL);
  TEST_ASSERT(native_lookup_key != NULL);
  TEST_ASSERT(live_class != NULL);
  TEST_ASSERT(ms_vm_define_native(&vm, &module, "alive_native", 0, test_native_noop));

  TEST_ASSERT(ms_table_set(&module.globals,
                           class_key,
                           ms_value_object((MsObject*) live_class),
                           &inserted_new));
  TEST_ASSERT(inserted_new);
  ms_vm_gc_track_object(&vm, &class_key->object);
  ms_vm_gc_track_object(&vm, &live_class->object);

  for (i = 0; i < module.globals.capacity; ++i) {
    if (module.globals.entries[i].key != NULL &&
        strcmp(module.globals.entries[i].key->bytes, "alive_native") == 0) {
      tracked_native_key = module.globals.entries[i].key;
      break;
    }
  }
  TEST_ASSERT(tracked_native_key != NULL);

  dead_outer = ms_list_new();
  dead_inner = ms_list_new();
  TEST_ASSERT(dead_outer != NULL);
  TEST_ASSERT(dead_inner != NULL);
  TEST_ASSERT(ms_value_array_append(&dead_outer->elements,
                                    ms_value_object((MsObject*) dead_inner)));
  ms_vm_gc_track_object(&vm, &dead_outer->object);
  ms_vm_gc_track_object(&vm, &dead_inner->object);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 6);

  ms_vm_gc_collect(&vm);
  TEST_ASSERT(vm.gc.collection_count == 1);
  TEST_ASSERT(vm.gc.free_count == 2);
  TEST_ASSERT(count_tracked_objects(&vm.gc) == 4);
  TEST_ASSERT(ms_table_get(&module.globals, class_key, &stored_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_get_class(stored_value, &live_class));
  TEST_ASSERT(live_class != NULL);
  TEST_ASSERT(ms_table_get(&module.globals,
                           native_lookup_key,
                           &stored_value,
                           &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_get_native_function(stored_value,
                                           &live_native_function));
  TEST_ASSERT(live_native_function != NULL);

  ms_vm_destroy(&vm);
  ms_module_destroy(&module);
  ms_native_function_free(live_native_function);
  ms_class_free(live_class);
  ms_string_free(tracked_native_key);
  ms_string_free(class_key);
  ms_string_free(native_lookup_key);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_gc_tracks_registered_objects() == 0);
  TEST_ASSERT(test_gc_tracks_native_registration_objects() == 0);
  TEST_ASSERT(test_compile_time_arena_does_not_touch_gc_state() == 0);
  TEST_ASSERT(test_temporary_roots_control_gc_lifetime() == 0);
  TEST_ASSERT(test_gc_reclaims_unreachable_object_graph() == 0);
  TEST_ASSERT(test_gc_keeps_live_object_while_reclaiming_dead_peer() == 0);
  TEST_ASSERT(test_gc_keeps_module_class_and_native_alive_while_reclaiming_dead_peer() == 0);
  return 0;
}
