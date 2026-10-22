// T013：string 对象单测（docs/tasks/T013 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_string.h"
#include "test_harness.h"
#include "vm/vm.h"

TEST(createAndHash) {  // 创建即定 hash/isAscii/charLen（22 §3）
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "abc", 3);
  EXPECT(ctx, s != NULL);
  EXPECT(ctx, s->head.type == MS_OBJ_STRING);
  EXPECT_EQ(ctx, 3, (long long)s->byteLen);
  EXPECT_EQ(ctx, 3, (long long)s->charLen);
  EXPECT(ctx, s->isAscii);
  EXPECT(ctx, s->hash != 0 && s->hash64 != 0);
  EXPECT_EQ(ctx, 0, memcmp("abc", s->bytes, 3));
  MsObjString* zh = msObjStringNew(&vm, "中文", 6);
  EXPECT(ctx, zh != NULL && !zh->isAscii);
  EXPECT_EQ(ctx, 6, (long long)zh->byteLen);
  EXPECT_EQ(ctx, 2, (long long)zh->charLen);
  EXPECT(ctx, zh->runeIndex == NULL);  // 惰性未构建
  msVmFree(&vm);
}

TEST(hashEqualConcat) {  // "abc" vs "ab"+"c"：hash 相等、值相等、指针不同
  MsVM vm;
  msVmInit(&vm);
  MsObjString* a = msObjStringNew(&vm, "abc", 3);
  MsObjString* b = msObjStringNew(&vm, "ab", 2);
  MsObjString* c = msObjStringConcat(&vm, b, a->bytes + 2, 1);
  EXPECT(ctx, c != NULL && a != c);
  EXPECT_EQ(ctx, (long long)a->hash, (long long)c->hash);
  EXPECT_EQ(ctx, (long long)a->hash64, (long long)c->hash64);
  EXPECT(ctx, msObjStringEqual(a, c));
  EXPECT(ctx, !msObjStringEqual(a, b));
  EXPECT(ctx, msObjStringCompare(a, c) == 0 && msObjStringCompare(b, a) < 0 &&
                  msObjStringCompare(c, b) > 0);
  msVmFree(&vm);
}

TEST(fromEscapedCreatesDecoded) {
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringFromEscaped(&vm, "a\\u{4E2D}b", 10);
  EXPECT(ctx, s != NULL);
  EXPECT_EQ(ctx, 5, (long long)s->byteLen);  // a + 中(3B) + b
  EXPECT_EQ(ctx, 3, (long long)s->charLen);
  EXPECT_EQ(ctx, 0, memcmp("a\xE4\xB8\xAD" "b", s->bytes, 5));
  msVmFree(&vm);
}

TEST(repeatAndEmpty) {  // Repeat 契约 + 空串 + FNV 向量锚定
  MsVM vm;
  msVmInit(&vm);
  MsObjString* r = msObjStringRepeat(&vm, msObjStringNew(&vm, "ab", 2), 3);
  EXPECT(ctx, r != NULL && r->byteLen == 6 && memcmp("ababab", r->bytes, 6) == 0);
  MsObjString* z = msObjStringRepeat(&vm, msObjStringNew(&vm, "x", 1), 0);
  EXPECT(ctx, z != NULL && z->byteLen == 0);
  MsObjString* e = msObjStringNew(&vm, "", 0);
  EXPECT(ctx, e != NULL && e->isAscii && e->charLen == 0);
  MsObjString* a = msObjStringNew(&vm, "a", 1);
  EXPECT_EQ(ctx, 0xE40C292C, (long long)a->hash);  // FNV-1a-32("a") 已知向量
  msVmFree(&vm);
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      createAndHash,
      hashEqualConcat,
      fromEscapedCreatesDecoded,
      repeatAndEmpty,
  };
  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  for (int i = 0; i < total; i++) {
    if (only < 0 || only == i) {
      tests[i](&ctx);
    }
  }
  if (ctx.failures == 0) {
    printf("all tests passed\n");
  }
  return ctx.failures == 0 ? 0 : 1;
}
