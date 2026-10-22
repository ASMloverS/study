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

TEST(runeIndexAndRuneAt) {  // "中文ab"[1] = '文'；ASCII 无表直查
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "中文ab", 8);
  ms_u32 cp = 0;
  EXPECT(ctx, msObjStringRuneAt(&vm, s, 1, &cp));
  EXPECT_EQ(ctx, 25991, (long long)cp);  // '文' U+6587
  EXPECT(ctx, msObjStringRuneAt(&vm, s, 2, &cp) && cp == 'a');
  EXPECT(ctx, msObjStringRuneAt(&vm, s, -1, &cp) && cp == 'b');
  EXPECT(ctx, msObjStringRuneAt(&vm, s, -4, &cp) && cp == 20013);  // '中' U+4E2D；-4 → rune 0
  EXPECT(ctx, !msObjStringRuneAt(&vm, s, 4, &cp));  // 越界
  EXPECT(ctx, !msObjStringRuneAt(&vm, s, -5, &cp));
  const int32_t* ri = msObjStringRuneIndex(&vm, s);
  EXPECT(ctx, ri != NULL);
  EXPECT_EQ(ctx, 0, ri[0]);
  EXPECT_EQ(ctx, 3, ri[1]);
  EXPECT_EQ(ctx, 6, ri[2]);
  EXPECT_EQ(ctx, 8, ri[4]);  // 末哨兵 = byteLen（表共 charLen+1 = 5 项，哨兵在下标 4）
  EXPECT_EQ(ctx, 1, msObjStringRuneAtByte(s, 3));
  EXPECT_EQ(ctx, 2, msObjStringRuneAtByte(s, 6));
  MsObjString* ascii = msObjStringNew(&vm, "abc", 3);
  EXPECT(ctx, msObjStringRuneIndex(&vm, ascii) == NULL);  // ASCII 快路径不建表
  EXPECT(ctx, msObjStringRuneAt(&vm, ascii, 1, &cp) && cp == 'b');
  msVmFree(&vm);
}

TEST(sliceRangeCanonical) {  // 规范化（T014 复用）
  ms_i64 st, sp, stp;
  EXPECT(ctx, msSliceRange(5, true, 1, true, 4, false, 0, &st, &sp, &stp));
  EXPECT_EQ(ctx, 1, st);
  EXPECT_EQ(ctx, 4, sp);
  EXPECT_EQ(ctx, 1, stp);
  EXPECT(ctx, msSliceRange(5, false, 0, false, 0, true, -1, &st, &sp, &stp));
  EXPECT_EQ(ctx, 4, st);
  EXPECT_EQ(ctx, -1, sp);
  EXPECT_EQ(ctx, -1, stp);
  EXPECT(ctx, msSliceRange(5, true, -3, false, 0, false, 0, &st, &sp, &stp));
  EXPECT_EQ(ctx, 2, st);
  EXPECT_EQ(ctx, 5, sp);
  EXPECT(ctx, msSliceRange(3, true, 10, false, 0, false, 0, &st, &sp, &stp));
  EXPECT_EQ(ctx, 3, st);  // 越界钳位
  EXPECT(ctx, !msSliceRange(5, false, 0, false, 0, true, 0, &st, &sp, &stp));  // 步进 0
  EXPECT(ctx, msSliceRange(3, false, 0, false, 0, true, INT64_MAX, &st, &sp, &stp));
  EXPECT_EQ(ctx, 0, st);
  EXPECT_EQ(ctx, 3, sp);
  EXPECT_EQ(ctx, 3, stp);  // 步幅钳到 len：观测等价，防拷贝循环 i += step 溢出
  EXPECT(ctx, msSliceRange(3, false, 0, false, 0, true, INT64_MIN, &st, &sp, &stp));
  EXPECT_EQ(ctx, 2, st);
  EXPECT_EQ(ctx, -1, sp);
  EXPECT_EQ(ctx, -3, stp);  // 负向同理
}

TEST(runeAtByteWithoutTable) {  // 线性扫描分支：const 签名不触发惰性建表
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "中a文", 7);  // 边界 0,3,4；charLen=3
  EXPECT_EQ(ctx, 0, msObjStringRuneAtByte(s, 0));  // 边界
  EXPECT_EQ(ctx, 1, msObjStringRuneAtByte(s, 3));  // 边界（'a'）
  EXPECT_EQ(ctx, 2, msObjStringRuneAtByte(s, 4));  // 边界（'文'起始）
  EXPECT_EQ(ctx, 0, msObjStringRuneAtByte(s, 1));  // '中'内部 → 前驱边界 0
  EXPECT_EQ(ctx, 2, msObjStringRuneAtByte(s, 5));  // '文'内部 → 前驱边界 4
  EXPECT(ctx, s->runeIndex == NULL);  // 证实全程未建表（确走线性分支）
  msVmFree(&vm);
}

TEST(stringSlice) {  // "a中b"：[0:2]="a中" [::2]="ab" [::-1]="b中a"
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "a中b", 5);  // a(1)+中(3)+b(1)
  bool zero = false;
  MsObjString* r = msObjStringSlice(&vm, s, true, 0, true, 2, false, 0, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 4 && memcmp("a\xE4\xB8\xAD", r->bytes, 4) == 0);
  r = msObjStringSlice(&vm, s, false, 0, false, 0, true, 2, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 2 && memcmp("ab", r->bytes, 2) == 0);
  r = msObjStringSlice(&vm, s, false, 0, false, 0, true, -1, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 5 && memcmp("b\xE4\xB8\xAD" "a", r->bytes, 5) == 0);
  r = msObjStringSlice(&vm, s, true, 0, true, 0, false, 0, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 0);  // 空切片
  EXPECT(ctx, msObjStringSlice(&vm, s, false, 0, false, 0, true, 0, &zero) == NULL && zero);
  MsObjString* as = msObjStringNew(&vm, "abc", 3);
  r = msObjStringSlice(&vm, as, false, 0, false, 0, true, -1, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 3 && memcmp("cba", r->bytes, 3) == 0);  // ASCII 快路径 + 负步
  msVmFree(&vm);
}

TEST(internDedup) {  // 同字面量两处出现指针相等（DoD）
  MsVM vm;
  msVmInit(&vm);
  MsObjString* a = msInternBytes(&vm, "hello", 5);
  MsObjString* b = msInternBytes(&vm, "hello", 5);
  EXPECT(ctx, a == b);
  EXPECT(ctx, a->head.flags & MS_OBJ_PERMANENT);
  MsObjString* c = msObjStringNew(&vm, "hello", 5);
  EXPECT(ctx, a != c);  // 运行时产物不强制 intern
  gcCollectAll(&vm);    // permanent 无 root 也存活
  EXPECT(ctx, a == msInternBytes(&vm, "hello", 5));
  msInternBytes(&vm, "", 0);  // 空串可 intern
  msVmFree(&vm);
}

TEST(internGrowth) {  // 负载再哈希：大批量 intern 后仍正确去重
  MsVM vm;
  msVmInit(&vm);
  char buf[32];
  for (int i = 0; i < 500; i++) {
    int n = snprintf(buf, sizeof(buf), "str-%d", i);
    EXPECT(ctx, msInternBytes(&vm, buf, n) == msInternBytes(&vm, buf, n));
  }
  for (int i = 0; i < 500; i++) {
    int n = snprintf(buf, sizeof(buf), "str-%d", i);
    EXPECT_EQ(ctx, 0, memcmp(buf, msInternBytes(&vm, buf, n)->bytes, (size_t)n));
  }
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
      runeIndexAndRuneAt,
      sliceRangeCanonical,
      runeAtByteWithoutTable,
      stringSlice,
      internDedup,
      internGrowth,
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
