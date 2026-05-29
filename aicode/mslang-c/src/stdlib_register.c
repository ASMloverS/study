#include "ms/stdlib_register.h"
#include "ms/module.h"

void ms_stdlib_register_all(MsVM* vm) {
    ms_vm_register_builtin_module(vm, "math",   ms_module_math_init);
    ms_vm_register_builtin_module(vm, "os",     ms_module_os_init);
    ms_vm_register_builtin_module(vm, "time",   ms_module_time_init);
    ms_vm_register_builtin_module(vm, "io",     ms_module_io_init);
    ms_vm_register_builtin_module(vm, "buffer", ms_module_buffer_init);
    ms_vm_register_builtin_module(vm, "hash",   ms_module_hash_init);
    ms_vm_register_builtin_module(vm, "log",    ms_module_log_init);
    ms_vm_register_builtin_module(vm, "net",    ms_module_net_init);
    ms_vm_register_builtin_module(vm, "debug",  ms_module_debug_init);
    ms_vm_register_builtin_module(vm, "gc",      ms_module_gc_init);
    ms_vm_register_builtin_module(vm, "strings", ms_module_strings_init);
    ms_vm_register_builtin_module(vm, "strconv", ms_module_strconv_init);
    ms_vm_register_builtin_module(vm, "fmt",     ms_module_fmt_init);
    ms_vm_register_builtin_module(vm, "json",    ms_module_json_init);
    ms_vm_register_builtin_module(vm, "errors",  ms_module_errors_init);
    ms_vm_register_builtin_module(vm, "slices",  ms_module_slices_init);
    ms_vm_register_builtin_module(vm, "maps",    ms_module_maps_init);
    ms_vm_register_builtin_module(vm, "sort",    ms_module_sort_init);
    ms_vm_register_builtin_module(vm, "set",     ms_module_set_init);
    ms_vm_register_builtin_module(vm, "heap",      ms_module_heap_init);
    ms_vm_register_builtin_module(vm, "itertools", ms_module_itertools_init);
    ms_vm_register_builtin_module(vm, "_rand",     ms_module_rand_init);
    ms_vm_register_builtin_module(vm, "random",    ms_module_random_init);
    ms_vm_register_builtin_module(vm, "base64",    ms_module_base64_init);
    ms_vm_register_builtin_module(vm, "hex",       ms_module_hex_init);
    ms_vm_register_builtin_module(vm, "bytes",     ms_module_bytes_init);
    ms_vm_register_builtin_module(vm, "regexp",    ms_module_regexp_init);
    ms_vm_register_builtin_module(vm, "path",      ms_module_path_init);
    ms_vm_register_builtin_module(vm, "bufio",     ms_module_bufio_init);
    ms_vm_register_builtin_module(vm, "unicode",   ms_module_unicode_init);
    ms_vm_register_builtin_module(vm, "binary",    ms_module_binary_init);
    ms_vm_register_builtin_module(vm, "csv",       ms_module_csv_init);
}
