#include "platform.h"
#include "memory.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/stat.h>
#endif

bool ms_platform_file_exists(const char* path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

bool ms_platform_write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len;
}

char* ms_platform_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(f);
        return NULL;
    }

    char* buf = MS_ALLOCATE(char, (size_t)file_size + 1);
    size_t bytes_read = fread(buf, 1, (size_t)file_size, f);
    buf[bytes_read] = '\0';
    fclose(f);
    return buf;
}

char* ms_platform_get_cwd(void) {
#ifdef _WIN32
    char* buf = MS_ALLOCATE(char, MAX_PATH);
    DWORD len = GetCurrentDirectoryA(MAX_PATH, buf);
    if (len == 0) {
        MS_FREE(char, buf, MAX_PATH);
        return NULL;
    }
    return buf;
#else
    long path_max = pathconf(".", _PC_PATH_MAX);
    if (path_max <= 0) path_max = 4096;
    char* buf = MS_ALLOCATE(char, (size_t)path_max);
    if (getcwd(buf, (size_t)path_max) == NULL) {
        MS_FREE(char, buf, (size_t)path_max);
        return NULL;
    }
    return buf;
#endif
}

char* ms_platform_join_path(const char* a, const char* b) {
    size_t a_len = strlen(a);
    size_t b_len = strlen(b);
    size_t total = a_len + 1 + b_len + 1;
    char* result = MS_ALLOCATE(char, total);

    memcpy(result, a, a_len);
    size_t pos = a_len;

    if (a_len > 0 && a[a_len - 1] != MS_PATH_SEPARATOR && b_len > 0 && b[0] != MS_PATH_SEPARATOR) {
        result[pos++] = MS_PATH_SEPARATOR;
    }

    memcpy(result + pos, b, b_len + 1);
    return result;
}

double ms_platform_get_time_seconds(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (double)(uli.QuadPart / 10000) / 1000.0 - 11644473600.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

char* ms_platform_get_env(const char* name) {
#ifdef _WIN32
    char* buf = NULL;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == NULL) {
        return NULL;
    }
    char* result = MS_ALLOCATE(char, strlen(buf) + 1);
    memcpy(result, buf, strlen(buf) + 1);
    free(buf);
    return result;
#else
    const char* val = getenv(name);
    if (val == NULL) return NULL;
    char* result = MS_ALLOCATE(char, strlen(val) + 1);
    memcpy(result, val, strlen(val) + 1);
    return result;
#endif
}

char* ms_platform_get_executable_path(void) {
#ifdef _WIN32
    char* buf = MS_ALLOCATE(char, MAX_PATH);
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0) {
        MS_FREE(char, buf, MAX_PATH);
        return NULL;
    }
    return buf;
#elif defined(__linux__)
    char* buf = MS_ALLOCATE(char, 4096);
    ssize_t len = readlink("/proc/self/exe", buf, 4095);
    if (len == -1) {
        MS_FREE(char, buf, 4096);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
#elif defined(__APPLE__)
    char* buf = MS_ALLOCATE(char, 4096);
    uint32_t size = 4096;
    if (_NSGetExecutablePath(buf, &size) != 0) {
        MS_FREE(char, buf, 4096);
        return NULL;
    }
    return buf;
#endif
}

static bool s_colors_enabled = false;

void ms_platform_enable_console_colors(void) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
#endif
    s_colors_enabled = true;
}

bool ms_platform_supports_colors(void) {
    return s_colors_enabled;
}

void ms_platform_set_console_color(const char* ansi_code) {
    if (!s_colors_enabled) return;
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleA(hOut, ansi_code, (DWORD)strlen(ansi_code), &written, NULL);
#else
    fprintf(stderr, "%s", ansi_code);
#endif
}

void ms_platform_reset_console_color(void) {
    ms_platform_set_console_color("\033[0m");
}
