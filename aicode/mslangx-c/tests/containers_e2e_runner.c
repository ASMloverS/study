#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/object.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/vm.h"
#include "ms/string.h"
#include "ms/table.h"
#include "ms/value.h"

static const int kExitParse = 65;
static const int kExitResolve = 66;
static const int kExitRuntime = 70;
static const int kExitIo = 74;

static void containers_runner_print_diagnostics(
    FILE *stream,
    const MsDiagnosticList *diagnostics) {
  size_t i;

  if (stream == NULL || diagnostics == NULL) {
    return;
  }

  for (i = 0; i < ms_diag_list_count(diagnostics); ++i) {
    const MsDiagnostic *diagnostic = ms_diag_list_at(diagnostics, i);

    if (diagnostic == NULL) {
      continue;
    }
    fprintf(stream,
            "phase=%s code=%s %s:%d:%d %s\n",
            diagnostic->phase != NULL ? diagnostic->phase : "<unknown>",
            diagnostic->code != NULL ? diagnostic->code : "<unknown>",
            diagnostic->span.file != NULL ? diagnostic->span.file : "<unknown>",
            diagnostic->span.line,
            diagnostic->span.column,
            diagnostic->message != NULL ? diagnostic->message : "<unknown>");
  }
}

static int containers_runner_exit_for_compile_result(
    MsCompileResult result,
    const MsDiagnosticList *diagnostics) {
  const MsDiagnostic *diagnostic = NULL;

  if (result == MS_COMPILE_RESULT_OK) {
    return 0;
  }
  if (ms_diag_list_count(diagnostics) > 0) {
    diagnostic = ms_diag_list_at(diagnostics, 0);
    if (diagnostic != NULL && diagnostic->phase != NULL) {
      if (strcmp(diagnostic->phase, "resolve") == 0) {
        return kExitResolve;
      }
      if (strcmp(diagnostic->phase, "parse") == 0 ||
          strcmp(diagnostic->phase, "lex") == 0) {
        return kExitParse;
      }
    }
  }

  if (result == MS_COMPILE_RESULT_PARSE_ERROR) {
    return kExitParse;
  }
  if (result == MS_COMPILE_RESULT_RESOLVE_ERROR) {
    return kExitResolve;
  }
  return 1;
}

static char *containers_runner_read_file(const char *path) {
  FILE *file;
  long size;
  size_t read_size;
  char *buffer;

  if (path == NULL) {
    return NULL;
  }

#if defined(_MSC_VER)
  if (fopen_s(&file, path, "rb") != 0) {
    file = NULL;
  }
#else
  file = fopen(path, "rb");
#endif
  if (file == NULL) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }

  size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  buffer = (char *) malloc((size_t) size + 1);
  if (buffer == NULL) {
    fclose(file);
    return NULL;
  }

  read_size = fread(buffer, 1, (size_t) size, file);
  fclose(file);
  if (read_size != (size_t) size) {
    free(buffer);
    return NULL;
  }

  buffer[size] = '\0';
  return buffer;
}

static MsCallResult native_len(MsVM *vm, int argc, const MsValue *argv) {
  int length = 0;

  (void) vm;
  if (argc != 1 || !ms_value_length(argv[0], &length)) {
    return ms_call_result_error();
  }

  return ms_call_result_ok(ms_value_number((double) length));
}

static MsCallResult native_empty_list(MsVM *vm, int argc, const MsValue *argv) {
  MsList *list;

  (void) vm;
  (void) argv;
  if (argc != 0) {
    return ms_call_result_error();
  }

  list = ms_list_new();
  if (list == NULL) {
    return ms_call_result_error();
  }
  return ms_call_result_ok(ms_value_object((MsObject *) list));
}

static MsCallResult native_filled_list(MsVM *vm, int argc, const MsValue *argv) {
  MsList *list;

  (void) vm;
  (void) argv;
  if (argc != 0) {
    return ms_call_result_error();
  }

  list = ms_list_new();
  if (list == NULL ||
      !ms_value_array_append(&list->elements, ms_value_number(1.0)) ||
      !ms_value_array_append(&list->elements, ms_value_number(2.0))) {
    return ms_call_result_error();
  }
  return ms_call_result_ok(ms_value_object((MsObject *) list));
}

static MsCallResult native_empty_tuple(MsVM *vm, int argc,
                                       const MsValue *argv) {
  MsTuple *tuple;

  (void) vm;
  (void) argv;
  if (argc != 0) {
    return ms_call_result_error();
  }

  tuple = ms_tuple_new();
  if (tuple == NULL) {
    return ms_call_result_error();
  }
  return ms_call_result_ok(ms_value_object((MsObject *) tuple));
}

static MsCallResult native_filled_tuple(MsVM *vm, int argc,
                                        const MsValue *argv) {
  MsTuple *tuple;

  (void) vm;
  (void) argv;
  if (argc != 0) {
    return ms_call_result_error();
  }

  tuple = ms_tuple_new();
  if (tuple == NULL ||
      !ms_value_array_append(&tuple->elements, ms_value_number(3.0)) ||
      !ms_value_array_append(&tuple->elements, ms_value_number(4.0))) {
    return ms_call_result_error();
  }
  return ms_call_result_ok(ms_value_object((MsObject *) tuple));
}

static MsCallResult native_empty_map(MsVM *vm, int argc, const MsValue *argv) {
  MsMap *map;

  (void) vm;
  (void) argv;
  if (argc != 0) {
    return ms_call_result_error();
  }

  map = ms_map_new();
  if (map == NULL) {
    return ms_call_result_error();
  }
  return ms_call_result_ok(ms_value_object((MsObject *) map));
}

static MsCallResult native_filled_map(MsVM *vm, int argc, const MsValue *argv) {
  MsMap *map;
  MsString *first_key;
  MsString *second_key;
  int inserted_new = 0;

  (void) vm;
  (void) argv;
  if (argc != 0) {
    return ms_call_result_error();
  }

  map = ms_map_new();
  first_key = ms_string_from_cstr("first");
  second_key = ms_string_from_cstr("second");
  if (map == NULL || first_key == NULL || second_key == NULL ||
      !ms_table_set(map->entries,
                    first_key,
                    ms_value_number(1.0),
                    &inserted_new) ||
      !inserted_new ||
      !ms_table_set(map->entries,
                    second_key,
                    ms_value_number(2.0),
                    &inserted_new) ||
      !inserted_new) {
    return ms_call_result_error();
  }
  return ms_call_result_ok(ms_value_object((MsObject *) map));
}

static int containers_runner_register_natives(MsVM *vm, MsModule *module) {
  return ms_vm_define_native(vm, module, "len", 1, native_len) &&
         ms_vm_define_native(vm, module, "empty_list", 0, native_empty_list) &&
         ms_vm_define_native(vm, module, "filled_list", 0, native_filled_list) &&
         ms_vm_define_native(vm, module, "empty_tuple", 0, native_empty_tuple) &&
         ms_vm_define_native(vm, module, "filled_tuple", 0, native_filled_tuple) &&
         ms_vm_define_native(vm, module, "empty_map", 0, native_empty_map) &&
         ms_vm_define_native(vm, module, "filled_map", 0, native_filled_map);
}

static int containers_runner_run_source(const char *file,
                                        const char *source,
                                        FILE *error_stream) {
  MsChunk chunk;
  MsDiagnosticList diagnostics;
  MsCompileResult compile_result;
  MsVM vm;
  MsModule module;
  int exit_code = 0;

  ms_chunk_init(&chunk);
  ms_diag_list_init(&diagnostics);
  compile_result = ms_compile_source(file, source, &chunk, &diagnostics);
  if (compile_result != MS_COMPILE_RESULT_OK) {
    containers_runner_print_diagnostics(error_stream, &diagnostics);
    exit_code =
        containers_runner_exit_for_compile_result(compile_result, &diagnostics);
    ms_diag_list_destroy(&diagnostics);
    ms_chunk_destroy(&chunk);
    return exit_code;
  }
  ms_diag_list_destroy(&diagnostics);

  ms_vm_init(&vm);
  ms_module_init(&module, file);
  ms_vm_set_current_module(&vm, &module);
  if (!containers_runner_register_natives(&vm, &module)) {
    ms_module_destroy(&module);
    ms_vm_destroy(&vm);
    ms_chunk_destroy(&chunk);
    return 1;
  }
  if (ms_vm_run_chunk(&vm, &chunk) != MS_VM_RESULT_OK) {
    containers_runner_print_diagnostics(error_stream, &vm.diagnostics);
    exit_code = kExitRuntime;
  }

  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_chunk_destroy(&chunk);
  return exit_code;
}

int main(int argc, char **argv) {
  char *source;
  int exit_code;

  if (argc != 2) {
    fprintf(stderr, "usage: containers_e2e_runner <script>\n");
    return kExitIo;
  }

  source = containers_runner_read_file(argv[1]);
  if (source == NULL) {
    fprintf(stderr, "error: failed to read script: %s\n", argv[1]);
    return kExitIo;
  }

  exit_code = containers_runner_run_source(argv[1], source, stderr);
  free(source);
  return exit_code;
}
