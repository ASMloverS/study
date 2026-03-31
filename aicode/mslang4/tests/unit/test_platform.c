#include "platform.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_platform_detection(void) {
    int platforms = MS_PLATFORM_WINDOWS + MS_PLATFORM_LINUX + MS_PLATFORM_MACOS;
    if (platforms == 0) {
        fprintf(stderr, "FAIL: no platform detected\n");
        exit(1);
    }
    if (platforms > 1) {
        fprintf(stderr, "FAIL: multiple platforms detected\n");
        exit(1);
    }
}

static void test_path_separator(void) {
#ifdef _WIN32
    if (MS_PATH_SEPARATOR != '\\') {
        fprintf(stderr, "FAIL: MS_PATH_SEPARATOR expected '\\\\' on Windows\n");
        exit(1);
    }
#else
    if (MS_PATH_SEPARATOR != '/') {
        fprintf(stderr, "FAIL: MS_PATH_SEPARATOR expected '/' on POSIX\n");
        exit(1);
    }
#endif
}

static void test_file_exists(void) {
    if (ms_platform_file_exists("nonexistent_file_xyz.txt")) {
        fprintf(stderr, "FAIL: nonexistent file should not exist\n");
        exit(1);
    }
}

static void test_write_read_roundtrip(void) {
    const char* path = "test_platform_tmp.txt";
    const char* content = "Hello, Maple!\nLine 2\n";

    if (!ms_platform_write_file(path, content)) {
        fprintf(stderr, "FAIL: write_file returned false\n");
        exit(1);
    }

    if (!ms_platform_file_exists(path)) {
        fprintf(stderr, "FAIL: file should exist after write\n");
        exit(1);
    }

    char* read_back = ms_platform_read_file(path);
    if (read_back == NULL) {
        fprintf(stderr, "FAIL: read_file returned NULL\n");
        remove(path);
        exit(1);
    }

    if (strcmp(read_back, content) != 0) {
        fprintf(stderr, "FAIL: read content differs from written content\n");
        fprintf(stderr, "  expected: %s\n", content);
        fprintf(stderr, "  got: %s\n", read_back);
        MS_FREE(char, read_back, strlen(read_back) + 1);
        remove(path);
        exit(1);
    }

    MS_FREE(char, read_back, strlen(read_back) + 1);
    remove(path);
}

static void test_get_cwd(void) {
    char* cwd = ms_platform_get_cwd();
    if (cwd == NULL) {
        fprintf(stderr, "FAIL: get_cwd returned NULL\n");
        exit(1);
    }
    if (strlen(cwd) == 0) {
        fprintf(stderr, "FAIL: get_cwd returned empty string\n");
        MS_FREE(char, cwd, strlen(cwd) + 1);
        exit(1);
    }
    MS_FREE(char, cwd, strlen(cwd) + 1);
}

static void test_join_path_basic(void) {
    char* result = ms_platform_join_path("a", "b");
    if (result == NULL) {
        fprintf(stderr, "FAIL: join_path returned NULL\n");
        exit(1);
    }

    const char expected_sep[] = { MS_PATH_SEPARATOR, '\0' };
    char expected[4] = "a";
    strcat(expected, expected_sep);
    strcat(expected, "b");

    if (strcmp(result, expected) != 0) {
        fprintf(stderr, "FAIL: join_path(\"a\", \"b\") expected \"%s\", got \"%s\"\n",
                expected, result);
        MS_FREE(char, result, strlen(result) + 1);
        exit(1);
    }
    MS_FREE(char, result, strlen(result) + 1);
}

static void test_get_time_monotonic(void) {
    double t1 = ms_platform_get_time_seconds();
    if (t1 <= 0.0) {
        fprintf(stderr, "FAIL: get_time_seconds should be positive, got %f\n", t1);
        exit(1);
    }
    double t2 = ms_platform_get_time_seconds();
    if (t2 < t1) {
        fprintf(stderr, "FAIL: time went backwards\n");
        exit(1);
    }
}

static void test_get_env(void) {
    char* path_val = ms_platform_get_env("PATH");
    if (path_val == NULL) {
        fprintf(stderr, "FAIL: PATH environment variable should exist\n");
        exit(1);
    }
    if (strlen(path_val) == 0) {
        fprintf(stderr, "FAIL: PATH should not be empty\n");
        MS_FREE(char, path_val, strlen(path_val) + 1);
        exit(1);
    }
    MS_FREE(char, path_val, strlen(path_val) + 1);

    char* nonexistent = ms_platform_get_env("MSLANG_NONEXISTENT_VAR_12345");
    if (nonexistent != NULL) {
        fprintf(stderr, "FAIL: nonexistent env var should return NULL\n");
        MS_FREE(char, nonexistent, strlen(nonexistent) + 1);
        exit(1);
    }
}

static void test_executable_path(void) {
    char* exe_path = ms_platform_get_executable_path();
    if (exe_path == NULL) {
        fprintf(stderr, "FAIL: get_executable_path returned NULL\n");
        exit(1);
    }
    if (strlen(exe_path) == 0) {
        fprintf(stderr, "FAIL: executable path is empty\n");
        MS_FREE(char, exe_path, strlen(exe_path) + 1);
        exit(1);
    }
    MS_FREE(char, exe_path, strlen(exe_path) + 1);
}

static void test_console_colors(void) {
    ms_platform_enable_console_colors();

    ms_platform_set_console_color("\033[31m");
    ms_platform_reset_console_color();

    ms_platform_set_console_color("invalid_code");
    ms_platform_reset_console_color();
}

int main(void) {
    test_platform_detection();
    test_path_separator();
    test_file_exists();
    test_write_read_roundtrip();
    test_get_cwd();
    test_join_path_basic();
    test_get_time_monotonic();
    test_get_env();
    test_executable_path();
    test_console_colors();
    printf("all platform tests passed\n");
    return 0;
}
