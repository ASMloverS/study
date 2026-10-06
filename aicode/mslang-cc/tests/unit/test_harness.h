// C 单测断言宏族（T002 §2）：失败打印 file:line 与 expected/got 并计数，不中断用例
// EXPECT_EQ/EXPECT_F64_EQ/EXPECT_STREQ 的实参会被求值两次，禁止传入带副作用的表达式
#ifndef INCLUDE_MS_TESTS_UNIT_TEST_HARNESS_H_
#define INCLUDE_MS_TESTS_UNIT_TEST_HARNESS_H_

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct TestCtx {
  int failures;
} TestCtx;

#define TEST(name) static void name(TestCtx* ctx)

// MSVC /W4 对宏实参折叠出的常量条件报 C4127（如 EXPECT(ctx, 1 == 1)），仅在这些断言宏内豁免
#if defined(_MSC_VER)
#define TEST_C4127_PUSH __pragma(warning(push))
#define TEST_C4127_DISABLE __pragma(warning(disable : 4127))
#define TEST_C4127_POP __pragma(warning(pop))
#else
#define TEST_C4127_PUSH
#define TEST_C4127_DISABLE
#define TEST_C4127_POP
#endif

#define EXPECT(ctx, cond)                                             \
  do {                                                                \
    TEST_C4127_PUSH                                                   \
    TEST_C4127_DISABLE                                                \
    if (!(cond)) {                                                    \
      (ctx)->failures++;                                              \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
    TEST_C4127_POP                                                    \
  } while (0)

#define EXPECT_EQ(ctx, expected, actual)                                                                       \
  do {                                                                                                         \
    TEST_C4127_PUSH                                                                                            \
    TEST_C4127_DISABLE                                                                                         \
    if ((long long)(expected) != (long long)(actual)) {                                                        \
      (ctx)->failures++;                                                                                       \
      fprintf(stderr, "FAIL %s:%d: %s expected %lld got %lld\n", __FILE__, __LINE__, #expected " == " #actual, \
              (long long)(expected), (long long)(actual));                                                     \
    }                                                                                                          \
    TEST_C4127_POP                                                                                             \
  } while (0)

#define EXPECT_F64_EQ(ctx, expected, actual)                                                                        \
  do {                                                                                                              \
    double f64Lhs_ = (double)(expected);                                                                            \
    double f64Rhs_ = (double)(actual);                                                                              \
    TEST_C4127_PUSH                                                                                                 \
    TEST_C4127_DISABLE                                                                                              \
    if (!(fabs(f64Lhs_ - f64Rhs_) <= 1e-9)) {                                                                       \
      (ctx)->failures++;                                                                                            \
      fprintf(stderr, "FAIL %s:%d: %s expected %g got %g\n", __FILE__, __LINE__, #expected " == " #actual, f64Lhs_, \
              f64Rhs_);                                                                                             \
    }                                                                                                               \
    TEST_C4127_POP                                                                                                  \
  } while (0)

#define EXPECT_STREQ(ctx, expected, actual)                                                                        \
  do {                                                                                                             \
    const char* strLhs_ = (expected);                                                                              \
    const char* strRhs_ = (actual);                                                                                \
    TEST_C4127_PUSH                                                                                                \
    TEST_C4127_DISABLE                                                                                             \
    if (strLhs_ == NULL || strRhs_ == NULL || strcmp(strLhs_, strRhs_) != 0) {                                     \
      (ctx)->failures++;                                                                                           \
      fprintf(stderr, "FAIL %s:%d: %s expected \"%s\" got \"%s\"\n", __FILE__, __LINE__, #expected " == " #actual, \
              strLhs_ != NULL ? strLhs_ : "(null)", strRhs_ != NULL ? strRhs_ : "(null)");                         \
    }                                                                                                              \
    TEST_C4127_POP                                                                                                 \
  } while (0)

#endif
