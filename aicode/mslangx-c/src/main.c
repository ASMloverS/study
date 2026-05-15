#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ms/cache/cache_file.h"
#include "ms/cache/source_loader.h"
#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/vm.h"

static const int kExitUsage = 64;
static const int kExitParse = 65;
static const int kExitResolve = 66;
static const int kExitRuntime = 70;
static const int kExitIo = 74;

static MsCallResult mslangc_native_gc_collect(MsVM* vm,
                                              int argc,
                                              const MsValue* argv) {
  (void) argc;
  (void) argv;

  ms_vm_gc_collect(vm);
  return ms_call_result_ok(ms_value_nil());
}

static const char* mslangc_string_or_unknown(const char* text) {
  if (text == NULL) {
    return "<unknown>";
  }
  return text;
}

static int mslangc_is_path_separator(char ch) {
  return ch == '/' || ch == '\\';
}

static int mslangc_has_suffix(const char *text, const char *suffix) {
  size_t text_length;
  size_t suffix_length;

  if (text == NULL || suffix == NULL) {
    return 0;
  }

  text_length = strlen(text);
  suffix_length = strlen(suffix);
  if (text_length < suffix_length) {
    return 0;
  }

  return strcmp(text + text_length - suffix_length, suffix) == 0;
}

static int mslangc_is_cache_file(const char *path) {
  return mslangc_has_suffix(path, ".msc");
}

static void mslangc_print_help(FILE *stream) {
  fprintf(stream,
          "usage: mslangc [--help] [--no-cache] [-e code] [script]\n"
          "\n"
          "Bootstrap CLI for the mslangc runtime.\n");
}

static void mslangc_print_diagnostics(FILE *stream,
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
            mslangc_string_or_unknown(diagnostic->phase),
            mslangc_string_or_unknown(diagnostic->code),
            mslangc_string_or_unknown(diagnostic->span.file),
            diagnostic->span.line,
            diagnostic->span.column,
            mslangc_string_or_unknown(diagnostic->message));
  }
}

static int mslangc_exit_for_compile_result(MsCompileResult result,
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

static int mslangc_run_source(const char *file,
                              int cache_enabled,
                              const char *source,
                              FILE *error_stream);

static int mslangc_execute_chunk(const char *file,
                                 int cache_enabled,
                                 const MsChunk *chunk,
                                 FILE *error_stream);

static int mslangc_run_cache_file(const char *file,
                                  int cache_enabled,
                                  FILE *error_stream) {
  MsCacheFileReadInfo read_info;
  MsChunk chunk;
  MsCacheFileStatus status;
  int exit_code = 0;
  const char *display_path;

  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&chunk);
  status = ms_cache_file_read(file, NULL, &read_info, &chunk);
  if (status != MS_CACHE_FILE_STATUS_OK) {
    switch (status) {
      case MS_CACHE_FILE_STATUS_NOT_FOUND:
      case MS_CACHE_FILE_STATUS_IO_ERROR:
      case MS_CACHE_FILE_STATUS_DIRECTORY_ERROR:
      case MS_CACHE_FILE_STATUS_WRITE_ERROR:
        fprintf(error_stream, "error: failed to read cache: %s\n", file);
        break;
      case MS_CACHE_FILE_STATUS_INCOMPATIBLE_VERSION:
        fprintf(error_stream, "error: incompatible cache file: %s\n", file);
        break;
      case MS_CACHE_FILE_STATUS_FORMAT_ERROR:
        fprintf(error_stream, "error: corrupt cache file: %s\n", file);
        break;
      case MS_CACHE_FILE_STATUS_INVALID_ARGUMENT:
      case MS_CACHE_FILE_STATUS_STALE_SOURCE:
      default:
        fprintf(error_stream, "error: invalid cache file: %s\n", file);
        break;
    }
    ms_cache_file_read_info_destroy(&read_info);
    ms_chunk_destroy(&chunk);
    return kExitIo;
  }

  display_path = read_info.display_path != NULL ? read_info.display_path : file;
  exit_code = mslangc_execute_chunk(display_path, cache_enabled, &chunk, error_stream);
  ms_cache_file_read_info_destroy(&read_info);
  ms_chunk_destroy(&chunk);
  return exit_code;
}

static char *mslangc_dirname(const char *path) {
  const char *last_separator = NULL;
  const char *cursor;
  size_t length;
  char *directory;

  if (path == NULL) {
    return NULL;
  }

  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (mslangc_is_path_separator(*cursor)) {
      last_separator = cursor;
    }
  }

  if (last_separator == NULL) {
    directory = (char *) malloc(2);
    if (directory != NULL) {
      directory[0] = '.';
      directory[1] = '\0';
    }
    return directory;
  }

  length = (size_t) (last_separator - path);
  if (length == 0) {
    length = 1;
  }

  directory = (char *) malloc(length + 1);
  if (directory == NULL) {
    return NULL;
  }

  memcpy(directory, path, length);
  directory[length] = '\0';
  return directory;
}

static char *mslangc_join_path(const char *left, const char *right) {
  size_t left_length;
  size_t right_length;
  size_t separator_length;
  size_t total_length;
  char *joined;

  if (left == NULL || right == NULL) {
    return NULL;
  }

  left_length = strlen(left);
  right_length = strlen(right);
  separator_length =
      left_length > 0 && !mslangc_is_path_separator(left[left_length - 1]) ? 1u : 0u;
  total_length = left_length + separator_length + right_length;
  joined = (char *) malloc(total_length + 1);
  if (joined == NULL) {
    return NULL;
  }

  memcpy(joined, left, left_length);
  if (separator_length > 0) {
    joined[left_length] = '/';
  }
  memcpy(joined + left_length + separator_length, right, right_length);
  joined[total_length] = '\0';
  return joined;
}

static int mslangc_directory_exists(const char *path) {
#if defined(_WIN32)
  struct _stat info;
#else
  struct stat info;
#endif

  if (path == NULL) {
    return 0;
  }

#if defined(_WIN32)
  return _stat(path, &info) == 0 && (info.st_mode & _S_IFDIR) != 0;
#else
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static void mslangc_add_module_search_roots(MsVM *vm, const char *file) {
  char *script_dir;
  char *cursor;

  if (vm == NULL || file == NULL || strcmp(file, "<inline>") == 0) {
    return;
  }

  script_dir = mslangc_dirname(file);
  if (script_dir == NULL) {
    return;
  }

  ms_vm_add_search_root(vm, script_dir);
  cursor = script_dir;
  while (cursor != NULL) {
    char *fixtures_root = mslangc_join_path(cursor, "fixtures/modules");
    char *parent;

    if (fixtures_root != NULL && mslangc_directory_exists(fixtures_root)) {
      ms_vm_add_search_root(vm, fixtures_root);
    }
    free(fixtures_root);

    parent = mslangc_dirname(cursor);
    if (parent == NULL || strcmp(parent, cursor) == 0) {
      free(parent);
      break;
    }
    free(cursor);
    cursor = parent;
  }

  free(cursor);
}

static int mslangc_execute_chunk(const char *file,
                                 int cache_enabled,
                                 const MsChunk *chunk,
                                 FILE *error_stream) {
  MsVM vm;
  MsModule module;
  int exit_code = 0;

  ms_vm_init(&vm);
  ms_vm_set_cache_enabled(&vm, cache_enabled);
  mslangc_add_module_search_roots(&vm, file);
  ms_module_init(&module, file);
  ms_vm_set_current_module(&vm, &module);
  ms_vm_define_native(&vm, &module, "__gc_collect__", 0, mslangc_native_gc_collect);
  if (ms_vm_run_chunk(&vm, chunk) != MS_VM_RESULT_OK) {
    mslangc_print_diagnostics(error_stream, &vm.diagnostics);
    exit_code = kExitRuntime;
  }

  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  return exit_code;
}

static int mslangc_run_script_file(const char *file,
                                   int cache_enabled,
                                   FILE *error_stream) {
  if (mslangc_is_cache_file(file)) {
    return mslangc_run_cache_file(file, cache_enabled, error_stream);
  }

  MsSourceLoadOptions options;
  MsSourceLoadResult result;
  MsDiagnosticList diagnostics;
  MsSourceLoadStatus load_status;
  int exit_code = 0;

  ms_source_load_options_init(&options);
  options.cache_enabled = cache_enabled;
  ms_source_load_result_init(&result);
  ms_diag_list_init(&diagnostics);

  load_status = ms_source_load_source(file, &options, &diagnostics, &result);
  if (load_status != MS_SOURCE_LOAD_STATUS_OK) {
    if (load_status == MS_SOURCE_LOAD_STATUS_COMPILE_ERROR) {
      mslangc_print_diagnostics(error_stream, &diagnostics);
      exit_code = mslangc_exit_for_compile_result(MS_COMPILE_RESULT_PARSE_ERROR,
                                                  &diagnostics);
    } else if (load_status == MS_SOURCE_LOAD_STATUS_IO_ERROR) {
      fprintf(error_stream, "error: failed to read script: %s\n", file);
      exit_code = kExitIo;
    } else {
      fprintf(error_stream, "error: unsupported script: %s\n", file);
      exit_code = kExitUsage;
    }

    ms_diag_list_destroy(&diagnostics);
    ms_source_load_result_destroy(&result);
    return exit_code;
  }
  ms_diag_list_destroy(&diagnostics);

  exit_code = mslangc_execute_chunk(result.source.display_path != NULL
                                        ? result.source.display_path
                                        : file,
                                    cache_enabled,
                                    &result.chunk,
                                    error_stream);
  ms_source_load_result_destroy(&result);
  return exit_code;
}

static int mslangc_run_source(const char *file,
                              int cache_enabled,
                              const char *source,
                              FILE *error_stream) {
  MsChunk chunk;
  MsDiagnosticList diagnostics;
  MsCompileResult compile_result;
  int exit_code = 0;

  ms_chunk_init(&chunk);
  ms_diag_list_init(&diagnostics);
  compile_result = ms_compile_source(file, source, &chunk, &diagnostics);
  if (compile_result != MS_COMPILE_RESULT_OK) {
    mslangc_print_diagnostics(error_stream, &diagnostics);
    exit_code = mslangc_exit_for_compile_result(compile_result, &diagnostics);
    ms_diag_list_destroy(&diagnostics);
    ms_chunk_destroy(&chunk);
    return exit_code;
  }
  ms_diag_list_destroy(&diagnostics);

  exit_code = mslangc_execute_chunk(file, cache_enabled, &chunk, error_stream);
  ms_chunk_destroy(&chunk);
  return exit_code;
}

int main(int argc, char **argv) {
  int cache_enabled = 1;
  const char *script_path = NULL;
  int i;

  if (argc <= 1) {
    mslangc_print_help(stdout);
    return 0;
  }

  for (i = 1; i < argc; ++i) {
    const char *arg = argv[i];

    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      mslangc_print_help(stdout);
      return 0;
    }
    if (strcmp(arg, "--no-cache") == 0) {
      cache_enabled = 0;
      continue;
    }
    if (strcmp(arg, "-e") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "error: missing argument for -e\n");
        return kExitUsage;
      }
      return mslangc_run_source("<inline>", cache_enabled, argv[i + 1], stderr);
    }
    if (arg[0] == '-') {
      fprintf(stderr, "error: unknown option: %s\n", arg);
      return kExitUsage;
    }
    script_path = arg;
    break;
  }

  if (script_path == NULL) {
    mslangc_print_help(stdout);
    return 0;
  }

  return mslangc_run_script_file(script_path, cache_enabled, stderr);
}
