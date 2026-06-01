#pragma once
#include "ms/vm.h"

/* Register all builtin stdlib modules into the VM registry.
   Called at the end of ms_vm_init.  Each init function is stored as a lazy
   pointer and only invoked when the user first imports that module. */
void ms_stdlib_register_all(MsVM* vm);

/* Per-module init stubs - implemented in src/stdlib/<name>.c (future tasks).
   Declared here so ms_stdlib_register_all can reference them without
   requiring the stdlib source files to exist yet. */
void ms_module_math_init  (MsVM* vm, MsObjModule* mod);
void ms_module_os_init    (MsVM* vm, MsObjModule* mod);
void ms_module_time_init  (MsVM* vm, MsObjModule* mod);
void ms_module_io_init    (MsVM* vm, MsObjModule* mod);
void ms_module_buffer_init(MsVM* vm, MsObjModule* mod);
void ms_module_hash_init  (MsVM* vm, MsObjModule* mod);
void ms_module_log_init   (MsVM* vm, MsObjModule* mod);
void ms_module_net_init   (MsVM* vm, MsObjModule* mod);
void ms_module_debug_init (MsVM* vm, MsObjModule* mod);
void ms_module_gc_init      (MsVM* vm, MsObjModule* mod);
void ms_module_strings_init (MsVM* vm, MsObjModule* mod);
void ms_module_strconv_init (MsVM* vm, MsObjModule* mod);
void ms_module_fmt_init     (MsVM* vm, MsObjModule* mod);
void ms_module_json_init    (MsVM* vm, MsObjModule* mod);
void ms_module_errors_init  (MsVM* vm, MsObjModule* mod);
void ms_module_slices_init  (MsVM* vm, MsObjModule* mod);
void ms_module_maps_init    (MsVM* vm, MsObjModule* mod);
void ms_module_sort_init    (MsVM* vm, MsObjModule* mod);
void ms_module_set_init     (MsVM* vm, MsObjModule* mod);
void ms_module_heap_init      (MsVM* vm, MsObjModule* mod);
void ms_module_itertools_init (MsVM* vm, MsObjModule* mod);
void ms_module_rand_init      (MsVM* vm, MsObjModule* mod);
void ms_module_random_init    (MsVM* vm, MsObjModule* mod);
void ms_module_base32_init    (MsVM* vm, MsObjModule* mod);
void ms_module_base64_init    (MsVM* vm, MsObjModule* mod);
void ms_module_hex_init       (MsVM* vm, MsObjModule* mod);
void ms_module_bytes_init     (MsVM* vm, MsObjModule* mod);
void ms_module_regexp_init    (MsVM* vm, MsObjModule* mod);
void ms_module_path_init      (MsVM* vm, MsObjModule* mod);
void ms_module_bufio_init     (MsVM* vm, MsObjModule* mod);
void ms_module_unicode_init   (MsVM* vm, MsObjModule* mod);
void ms_module_binary_init    (MsVM* vm, MsObjModule* mod);
void ms_module_csv_init       (MsVM* vm, MsObjModule* mod);
void ms_module_cmp_init       (MsVM* vm, MsObjModule* mod);
void ms_module_deque_init      (MsVM* vm, MsObjModule* mod);
void ms_module_linkedlist_init (MsVM* vm, MsObjModule* mod);
void ms_module_ring_init       (MsVM* vm, MsObjModule* mod);
void ms_module_flag_init       (MsVM* vm, MsObjModule* mod);
void ms_module_url_init        (MsVM* vm, MsObjModule* mod);
void ms_module_sync_init       (MsVM* vm, MsObjModule* mod);
void ms_module_context_init    (MsVM* vm, MsObjModule* mod);
void ms_module_bits_init       (MsVM* vm, MsObjModule* mod);
void ms_module_hmac_init       (MsVM* vm, MsObjModule* mod);
void ms_module_testing_init    (MsVM* vm, MsObjModule* mod);
void ms_module_template_init   (MsVM* vm, MsObjModule* mod);
