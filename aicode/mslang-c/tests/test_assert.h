#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: " #cond "\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define TEST_ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            fprintf(stderr, "FAIL %s:%d: " #a " != " #b "\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define TEST_ASSERT_STR_EQ(a, b) \
    do { \
        const char* _a = (a); \
        const char* _b = (b); \
        if (_a == NULL || _b == NULL) { \
            fprintf(stderr, "FAIL %s:%d: " #a " or " #b " is NULL\n", __FILE__, __LINE__); \
            exit(1); \
        } \
        if (strcmp(_a, _b) != 0) { \
            fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); \
            exit(1); \
        } \
    } while (0)
