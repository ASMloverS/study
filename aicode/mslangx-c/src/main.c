#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/vm.h"

static const int kExitUsage = 64;
static const int kExitParse = 65;
static const int kExitResolve = 66;
static const int kExitRuntime = 70;
static const int kExitIo = 74;

static void mslangc_print_help(FILE *stream) {
  fprintf(stream,
          "usage: mslangc [--help] [-e code] [script]\n"
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
            diagnostic->phase != NULL ? diagnostic->phase : "<unknown>",
            diagnostic->code != NULL ? diagnostic->code : "<unknown>",
            diagnostic->span.file != NULL ? diagnostic->span.file : "<unknown>",
            diagnostic->span.line,
            diagnostic->span.column,
            diagnostic->message != NULL ? diagnostic->message : "<unknown>");
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

static char *mslangc_read_file(const char *path) {
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

static char *mslangc_dirname(const char *path) {
  const char *last_separator = NULL;
  const char *cursor;
  size_t length;
  char *directory;

  if (path == NULL) {
    return NULL;
  }

  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') {
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
  int needs_separator;
  char *joined;

  if (left == NULL || right == NULL) {
    return NULL;
  }

  left_length = strlen(left);
  right_length = strlen(right);
  needs_separator =
      left_length > 0 && left[left_length - 1] != '/' && left[left_length - 1] != '\\';
  joined =
      (char *) malloc(left_length + (needs_separator ? 1u : 0u) + right_length + 1);
  if (joined == NULL) {
    return NULL;
  }

  memcpy(joined, left, left_length);
  if (needs_separator) {
    joined[left_length] = '/';
  }
  memcpy(joined + left_length + (needs_separator ? 1u : 0u), right, right_length);
  joined[left_length + (needs_separator ? 1u : 0u) + right_length] = '\0';
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

static int mslangc_run_source(const char *file,
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
    mslangc_print_diagnostics(error_stream, &diagnostics);
    exit_code = mslangc_exit_for_compile_result(compile_result, &diagnostics);
    ms_diag_list_destroy(&diagnostics);
    ms_chunk_destroy(&chunk);
    return exit_code;
  }
  ms_diag_list_destroy(&diagnostics);

  ms_vm_init(&vm);
  mslangc_add_module_search_roots(&vm, file);
  ms_module_init(&module, file);
  ms_vm_set_current_module(&vm, &module);
  if (ms_vm_run_chunk(&vm, &chunk) != MS_VM_RESULT_OK) {
    mslangc_print_diagnostics(error_stream, &vm.diagnostics);
    exit_code = kExitRuntime;
  }

  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_chunk_destroy(&chunk);
  return exit_code;
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    mslangc_print_help(stdout);
    return 0;
  }

  if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
    mslangc_print_help(stdout);
    return 0;
  }

  if (strcmp(argv[1], "-e") == 0) {
    if (argc < 3) {
      fprintf(stderr, "error: missing argument for -e\n");
      return kExitUsage;
    }
    return mslangc_run_source("<inline>", argv[2], stderr);
  }

  if (argv[1][0] == '-') {
    fprintf(stderr, "error: unknown option: %s\n", argv[1]);
    return kExitUsage;
  }

  {
    char *source = mslangc_read_file(argv[1]);
    int exit_code;

    if (source == NULL) {
      fprintf(stderr, "error: failed to read script: %s\n", argv[1]);
      return kExitIo;
    }

    exit_code = mslangc_run_source(argv[1], source, stderr);
    free(source);
    return exit_code;
  }
}
