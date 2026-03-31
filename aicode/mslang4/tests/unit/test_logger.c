#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_log_basic(void) {
    FILE* tmp = tmpfile();
    if (tmp == NULL) {
        fprintf(stderr, "FAIL: tmpfile() returned NULL\n");
        exit(1);
    }

    ms_logger_set_output(tmp);
    ms_logger_enable_colors(false);
    ms_logger_enable_timestamp(false);
    ms_logger_set_level(MS_LOG_TRACE);

    ms_logger_log(MS_LOG_INFO, "test.c", 42, "test_func", "hello %s", "world");

    fflush(tmp);
    rewind(tmp);

    char buf[512] = {0};
    fread(buf, 1, sizeof(buf) - 1, tmp);
    fclose(tmp);

    if (strstr(buf, "INFO") == NULL) {
        fprintf(stderr, "FAIL: output missing 'INFO', got: %s\n", buf);
        exit(1);
    }
    if (strstr(buf, "test.c:42") == NULL) {
        fprintf(stderr, "FAIL: output missing 'test.c:42', got: %s\n", buf);
        exit(1);
    }
    if (strstr(buf, "test_func") == NULL) {
        fprintf(stderr, "FAIL: output missing 'test_func', got: %s\n", buf);
        exit(1);
    }
    if (strstr(buf, "hello world") == NULL) {
        fprintf(stderr, "FAIL: output missing 'hello world', got: %s\n", buf);
        exit(1);
    }
}

static void test_level_filtering(void) {
    FILE* tmp = tmpfile();
    ms_logger_set_output(tmp);
    ms_logger_enable_colors(false);
    ms_logger_enable_timestamp(false);
    ms_logger_set_level(MS_LOG_WARN);

    ms_logger_log(MS_LOG_TRACE, "f.c", 1, "fn", "trace msg");
    ms_logger_log(MS_LOG_DEBUG, "f.c", 1, "fn", "debug msg");
    ms_logger_log(MS_LOG_INFO,  "f.c", 1, "fn", "info msg");
    ms_logger_log(MS_LOG_WARN,  "f.c", 1, "fn", "warn msg");
    ms_logger_log(MS_LOG_ERROR, "f.c", 1, "fn", "error msg");

    fflush(tmp);
    rewind(tmp);

    char buf[512] = {0};
    fread(buf, 1, sizeof(buf) - 1, tmp);
    fclose(tmp);

    if (strstr(buf, "trace msg") != NULL) {
        fprintf(stderr, "FAIL: TRACE should be suppressed at WARN level\n");
        exit(1);
    }
    if (strstr(buf, "debug msg") != NULL) {
        fprintf(stderr, "FAIL: DEBUG should be suppressed at WARN level\n");
        exit(1);
    }
    if (strstr(buf, "info msg") != NULL) {
        fprintf(stderr, "FAIL: INFO should be suppressed at WARN level\n");
        exit(1);
    }
    if (strstr(buf, "warn msg") == NULL) {
        fprintf(stderr, "FAIL: WARN should appear\n");
        exit(1);
    }
    if (strstr(buf, "error msg") == NULL) {
        fprintf(stderr, "FAIL: ERROR should appear\n");
        exit(1);
    }
}

static void test_convenience_macros(void) {
    FILE* tmp = tmpfile();
    ms_logger_set_output(tmp);
    ms_logger_enable_colors(false);
    ms_logger_enable_timestamp(false);
    ms_logger_set_level(MS_LOG_TRACE);

    ms_logger_trace("trace %d", 1);
    ms_logger_debug("debug %d", 2);
    ms_logger_info("info %d", 3);
    ms_logger_warn("warn %d", 4);
    ms_logger_error("error %d", 5);
    ms_logger_fatal("fatal %d", 6);

    fflush(tmp);
    rewind(tmp);

    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf) - 1, tmp);
    fclose(tmp);

    if (strstr(buf, "TRACE") == NULL || strstr(buf, "trace 1") == NULL) {
        fprintf(stderr, "FAIL: trace macro output missing\n");
        exit(1);
    }
    if (strstr(buf, "DEBUG") == NULL || strstr(buf, "debug 2") == NULL) {
        fprintf(stderr, "FAIL: debug macro output missing\n");
        exit(1);
    }
    if (strstr(buf, "INFO") == NULL || strstr(buf, "info 3") == NULL) {
        fprintf(stderr, "FAIL: info macro output missing\n");
        exit(1);
    }
    if (strstr(buf, "WARN") == NULL || strstr(buf, "warn 4") == NULL) {
        fprintf(stderr, "FAIL: warn macro output missing\n");
        exit(1);
    }
    if (strstr(buf, "ERROR") == NULL || strstr(buf, "error 5") == NULL) {
        fprintf(stderr, "FAIL: error macro output missing\n");
        exit(1);
    }
    if (strstr(buf, "FATAL") == NULL || strstr(buf, "fatal 6") == NULL) {
        fprintf(stderr, "FAIL: fatal macro output missing\n");
        exit(1);
    }
}

static void test_colored_output(void) {
    FILE* tmp = tmpfile();
    ms_logger_set_output(tmp);
    ms_logger_enable_timestamp(false);
    ms_logger_set_level(MS_LOG_TRACE);

    ms_logger_enable_colors(true);
    ms_logger_log(MS_LOG_ERROR, "f.c", 1, "fn", "red error");

    fflush(tmp);
    rewind(tmp);

    char buf[512] = {0};
    fread(buf, 1, sizeof(buf) - 1, tmp);

    if (strstr(buf, "\033[") == NULL) {
        fprintf(stderr, "FAIL: colored output should contain ANSI escape codes\n");
        exit(1);
    }
    if (strstr(buf, "red error") == NULL) {
        fprintf(stderr, "FAIL: colored output should still contain message\n");
        exit(1);
    }

    fclose(tmp);
    ms_logger_enable_colors(false);
}

static void test_compile_time_level(void) {
    if (MS_LOG_LEVEL < 0 || MS_LOG_LEVEL > 6) {
        fprintf(stderr, "FAIL: MS_LOG_LEVEL out of range: %d\n", MS_LOG_LEVEL);
        exit(1);
    }
}

int main(void) {
    test_log_basic();
    printf("test_log_basic passed\n");
    test_level_filtering();
    printf("test_level_filtering passed\n");
    test_convenience_macros();
    printf("test_convenience_macros passed\n");
    test_colored_output();
    printf("test_colored_output passed\n");
    test_compile_time_level();
    printf("test_compile_time_level passed\n");
    return 0;
}
