// T015：map 数据面 + 数值塔键 + 墓碑再哈希单测（docs/tasks/T015 §5.1）
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "gc/gc.h"
#include "obj/obj_list.h"
#include "obj/obj_map.h"
#include "obj/obj_set.h"
#include "obj/obj_string.h"
#include "obj/obj_tuple.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
#include "test_harness.h"
#include "vm/vm.h"

// ---------- 执行面桥（Task 4+：全管线跑 .ms 源码） ----------

typedef struct RunResult {
  char* out;
  bool compileOk;
  bool hasError;
  char err[240];
  int32_t errLine;
} RunResult;

static char* slurpTmp(FILE* f) {
  fflush(f);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = malloc((size_t)len + 1);
  size_t got = fread(buf, 1, (size_t)len, f);
  buf[got] = '\0';
  fclose(f);
  return buf;
}

static RunResult runSrc(const char* src) {
  RunResult r = {NULL, false, false, "", -1};
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", &arena, &diags);
  AstNode* mod = parserParseModule(&p);
  if (mod != NULL) {
    Binder b;
    binderInit(&b, &arena, "t.ms", &diags);
    binderBind(&b, mod);
    binderDestroy(&b);
    Chunk c;
    if (compilerCompile(mod, &c, "t.ms", &diags)) {
      r.compileOk = true;
      MsVM vm;
      msVmInit(&vm);
      FILE* f = tmpfile();
      vm.out = f;
      msVmRun(&vm, &c);
      r.hasError = vm.hasError;
      snprintf(r.err, sizeof(r.err), "%s", vm.errMsg);
      r.errLine = vm.errLine;
      r.out = slurpTmp(f);
      msVmFree(&vm);
      chunkFree(&c);
    }
  }
  astArenaDestroy(&arena);
  if (r.out == NULL) {
    r.out = calloc(1, 1);
  }
  return r;
}

// ---------- 数据面（Task 2） ----------

// map API 键值按指针传参：整值复合字面量取址便捷构造（块内生存期覆盖调用表达式）
#define MV_INT(n) (&(MsValue){.kind = MS_VAL_I64, .as = {.i = (n)}})

TEST(mapInsertionOrder) {  // put b,a,c → remove a → 再 put a：seq 链 b,c,a（02 §5.2 插入序）
  MsVM vm;
  msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  EXPECT(ctx, m != NULL);
  msVmPush(&vm, msValueObj(&m->head));  // root：map 为 GC 根（intern 键永久存活）
  MsValue kb = msValueObj(&msInternBytes(&vm, "b", 1)->head);
  MsValue ka = msValueObj(&msInternBytes(&vm, "a", 1)->head);
  MsValue kc = msValueObj(&msInternBytes(&vm, "c", 1)->head);
  MsMapOp op = msObjMapPut(&vm, m, &kb, MV_INT(1));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  op = msObjMapPut(&vm, m, &ka, MV_INT(2));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  op = msObjMapPut(&vm, m, &kc, MV_INT(3));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  MsValue old = msValueNil();
  op = msObjMapRemove(m, &ka, &old);
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 2, old.as.i);
  op = msObjMapPut(&vm, m, &ka, MV_INT(4));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 3, m->liveCount);
  const char* expectKeys[] = {"b", "c", "a"};  // 删除后再插入 → 链尾
  int32_t n = 0;
  for (ms_i64 s = msObjMapFirst(m); s >= 0; s = msObjMapNext(m, s)) {
    const MsValue* k = msObjMapKeyAt(m, s);
    EXPECT(ctx, n < 3 && msIsString(k) && ((MsObjString*)k->as.obj)->byteLen == 1 &&
                     ((MsObjString*)k->as.obj)->bytes[0] == expectKeys[n][0]);
    n++;
  }
  EXPECT_EQ(ctx, 3, n);
  MsValue out = msValueNil();
  op = msObjMapGet(m, &ka, &out);
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 4, out.as.i);
  msVmFree(&vm);
}

TEST(mapNumericTowerKeys) {  // m[1]=1; m[1.0]=2 → 单键值 2（02 §7 hash(1)==hash(1.0) 同槽）
  MsVM vm;
  msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  EXPECT(ctx, m != NULL);
  msVmPush(&vm, msValueObj(&m->head));
  MsValue k1 = msValueInt(1);
  MsValue kf = msValueFloat(1.0);
  MsMapOp op = msObjMapPut(&vm, m, &k1, MV_INT(1));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  op = msObjMapPut(&vm, m, &kf, MV_INT(2));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 1, m->liveCount);
  MsValue out = msValueNil();
  op = msObjMapGet(m, &k1, &out);
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 2, out.as.i);
  msVmFree(&vm);
}

TEST(mapNumericEdgeDomains) {  // -0.0/0 同键；±2^63 域边界（钉死 UBSan 行为）；同容墓碑净化
  MsVM vm;
  msVmInit(&vm);
  MsValue out = msValueNil();
  // -0.0 / 0(int) / 0.0 数值塔同域（02 §7）：三写单键
  MsObjMap* m1 = msObjMapNew(&vm);
  EXPECT(ctx, m1 != NULL);
  msVmPush(&vm, msValueObj(&m1->head));
  MsValue kneg = msValueFloat(-0.0);
  MsValue kint = msValueInt(0);
  MsValue kpos = msValueFloat(0.0);
  EXPECT(ctx, msObjMapPut(&vm, m1, &kneg, MV_INT(1)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m1, &kint, MV_INT(2)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m1, &kpos, MV_INT(3)) == MS_MAP_OK);
  EXPECT_EQ(ctx, 1, m1->liveCount);
  EXPECT(ctx, msObjMapGet(m1, &kint, &out) == MS_MAP_OK && out.as.i == 3);
  EXPECT(ctx, msObjMapGet(m1, &kneg, &out) == MS_MAP_OK && out.as.i == 3);
  // -2^63 ↔ INT64_MIN：i64 精确表示域边界，同 hash 同键；+2^63 越域位型仅自一致
  MsObjMap* m2 = msObjMapNew(&vm);
  EXPECT(ctx, m2 != NULL);
  msVmPush(&vm, msValueObj(&m2->head));
  MsValue kmin = msValueFloat(-9223372036854775808.0);
  MsValue kimin = msValueInt(INT64_MIN);
  ms_u64 h1 = 0;
  ms_u64 h2 = 0;
  EXPECT(ctx, msValueHash(&kmin, &h1) == MS_HASH_OK);
  EXPECT(ctx, msValueHash(&kimin, &h2) == MS_HASH_OK);
  EXPECT(ctx, h1 == h2);  // u64 全位比较（EXPECT_EQ 经 long long 截高位）
  EXPECT(ctx, msObjMapPut(&vm, m2, &kmin, MV_INT(1)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m2, &kimin, MV_INT(2)) == MS_MAP_OK);
  EXPECT_EQ(ctx, 1, m2->liveCount);  // 同键改值
  EXPECT(ctx, msObjMapGet(m2, &kmin, &out) == MS_MAP_OK && out.as.i == 2);
  MsValue kmax = msValueFloat(9223372036854775808.0);
  ms_u64 h3 = 0;
  ms_u64 h4 = 0;
  EXPECT(ctx, msValueHash(&kmax, &h3) == MS_HASH_OK);
  EXPECT(ctx, msValueHash(&kmax, &h4) == MS_HASH_OK);
  EXPECT(ctx, h3 == h4);  // 位型自一致
  EXPECT(ctx, msObjMapPut(&vm, m2, &kmax, MV_INT(5)) == MS_MAP_OK);
  EXPECT_EQ(ctx, 2, m2->liveCount);  // 与 int 域键共存
  EXPECT(ctx, msObjMapGet(m2, &kmax, &out) == MS_MAP_OK && out.as.i == 5);
  // 同容净化：put 6（第 6 次门控 (5+1)*4=24 ≤ 24 不扩容）→ remove 4（used=6 墓碑 4）→
  // 再 put 门控 (6+1)*4=28 > 24 且 live*2=4 < 8 → 原容 rehash 净化墓碑
  MsObjMap* m3 = msObjMapNew(&vm);
  EXPECT(ctx, m3 != NULL);
  msVmPush(&vm, msValueObj(&m3->head));
  for (int i = 1; i <= 6; i++) {
    MsValue k = msValueInt(i);
    EXPECT(ctx, msObjMapPut(&vm, m3, &k, MV_INT(i)) == MS_MAP_OK);
  }
  EXPECT_EQ(ctx, 6, m3->liveCount);
  EXPECT_EQ(ctx, 8, m3->cap);
  for (int i = 1; i <= 4; i++) {
    MsValue k = msValueInt(i);
    EXPECT(ctx, msObjMapRemove(m3, &k, NULL) == MS_MAP_OK);
  }
  EXPECT_EQ(ctx, 2, m3->liveCount);
  EXPECT_EQ(ctx, 6, m3->usedCount);  // 墓碑占位
  MsValue k7 = msValueInt(7);
  EXPECT(ctx, msObjMapPut(&vm, m3, &k7, MV_INT(7)) == MS_MAP_OK);
  EXPECT_EQ(ctx, 8, m3->cap);        // 同容净化（非倍容）
  EXPECT_EQ(ctx, 3, m3->usedCount);  // 墓碑已清：used 回落至 live
  EXPECT_EQ(ctx, 3, m3->liveCount);
  ms_i64 wantKeys[] = {5, 6, 7};  // 幸存原序（5,6）+ 新键在尾（7）
  int32_t n = 0;
  for (ms_i64 s = msObjMapFirst(m3); s >= 0; s = msObjMapNext(m3, s)) {
    const MsValue* k = msObjMapKeyAt(m3, s);
    EXPECT(ctx, n < 3 && msIsI64(k) && k->as.i == wantKeys[n]);
    n++;
  }
  EXPECT_EQ(ctx, 3, n);
  msVmFree(&vm);
}

TEST(mapUnhashableKey) {  // list 键 MS_MAP_UNHASHABLE（TypeError 源）；bool 与 int 键分离
  MsVM vm;
  msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  EXPECT(ctx, m != NULL);
  msVmPush(&vm, msValueObj(&m->head));
  MsObjList* list = msObjListNew(&vm);
  EXPECT(ctx, list != NULL);
  msVmPush(&vm, msValueObj(&list->head));  // root
  MsValue kl = msValueObj(&list->head);
  MsValue out = msValueNil();
  MsMapOp op = msObjMapPut(&vm, m, &kl, MV_INT(1));
  EXPECT_EQ(ctx, MS_MAP_UNHASHABLE, op);
  op = msObjMapGet(m, &kl, &out);
  EXPECT_EQ(ctx, MS_MAP_UNHASHABLE, op);
  MsValue kt = msValueBool(true);
  MsValue ki = msValueInt(1);
  op = msObjMapPut(&vm, m, &kt, MV_INT(9));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  op = msObjMapPut(&vm, m, &ki, MV_INT(8));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 2, m->liveCount);  // true 与 1 不同键（02 §2 bool≠int）
  op = msObjMapGet(m, &kt, &out);
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 9, out.as.i);
  op = msObjMapGet(m, &ki, &out);
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 8, out.as.i);
  msVmFree(&vm);
}

TEST(mapRehashStress) {  // 删除密集场景（DoD）：千级增删后键值保留、插入序稳定
  MsVM vm;
  msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  EXPECT(ctx, m != NULL);
  msVmPush(&vm, msValueObj(&m->head));
  for (int i = 0; i < 1000; i++) {  // 偶键 0..1998（值 = 键+1）
    MsValue k = msValueInt((ms_i64)i * 2);
    EXPECT(ctx, msObjMapPut(&vm, m, &k, MV_INT((ms_i64)i * 2 + 1)) == MS_MAP_OK);
  }
  for (int i = 0; i < 500; i++) {  // 删偶键 0..998（落墓碑）
    MsValue k = msValueInt((ms_i64)i * 2);
    MsValue old = msValueNil();
    EXPECT(ctx, msObjMapRemove(m, &k, &old) == MS_MAP_OK && old.as.i == (ms_i64)i * 2 + 1);
  }
  for (int i = 0; i < 500; i++) {  // 奇键 1..999（值 = 键+1；复用墓碑槽）
    MsValue k = msValueInt((ms_i64)i * 2 + 1);
    EXPECT(ctx, msObjMapPut(&vm, m, &k, MV_INT((ms_i64)i * 2 + 2)) == MS_MAP_OK);
  }
  EXPECT_EQ(ctx, 1000, m->liveCount);
  EXPECT(ctx, m->cap > 64);  // 千级条目必经倍容（初始 8）
  MsValue out = msValueNil();
  for (int i = 0; i < 500; i++) {  // 抽查奇键值
    MsValue k = msValueInt((ms_i64)i * 2 + 1);
    EXPECT(ctx, msObjMapGet(m, &k, &out) == MS_MAP_OK && out.as.i == (ms_i64)i * 2 + 2);
  }
  int32_t n = 0;  // 链序：剩余偶键 1000..1998（原序）→ 奇键 1..999（插入序）
  bool orderOk = true;
  for (ms_i64 s = msObjMapFirst(m); s >= 0; s = msObjMapNext(m, s)) {
    const MsValue* k = msObjMapKeyAt(m, s);
    ms_i64 want = n < 500 ? (ms_i64)(n + 500) * 2 : (ms_i64)(n - 500) * 2 + 1;
    if (!msIsI64(k) || k->as.i != want) {
      orderOk = false;
    }
    n++;
  }
  EXPECT(ctx, orderOk);
  EXPECT_EQ(ctx, 1000, n);
  msVmFree(&vm);
}

TEST(mapNaNKeyIEEE) {  // NaN != NaN → 每次插入新键；get NaN 恒 NOENT（IEEE 语义）
  MsVM vm;
  msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  EXPECT(ctx, m != NULL);
  msVmPush(&vm, msValueObj(&m->head));
  MsValue k = msValueFloat(NAN);
  MsMapOp op = msObjMapPut(&vm, m, &k, MV_INT(1));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  op = msObjMapPut(&vm, m, &k, MV_INT(2));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 2, m->liveCount);  // 永不相等 → 两键
  MsValue out = msValueNil();
  op = msObjMapGet(m, &k, &out);
  EXPECT_EQ(ctx, MS_MAP_NOENT, op);
  msVmFree(&vm);
}

TEST(mapTupleKeys) {  // tuple 键 put/get 往返 + hash 缓存；含 list 元素不可 hash；等值同槽
  MsVM vm;
  msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  EXPECT(ctx, m != NULL);
  msVmPush(&vm, msValueObj(&m->head));
  MsObjTuple* t1 = msObjTupleNew(&vm, 2);
  EXPECT(ctx, t1 != NULL);
  msVmPush(&vm, msValueObj(&t1->head));  // root：tuple 键非永久，落栈防误收
  t1->items[0] = msValueInt(1);
  t1->items[1] = msValueInt(2);
  MsValue k1 = msValueObj(&t1->head);
  MsMapOp op = msObjMapPut(&vm, m, &k1, MV_INT(7));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT(ctx, t1->hashValid);  // 首次入 map 即缓存（22 §4 惰性）
  MsValue out = msValueNil();
  op = msObjMapGet(m, &k1, &out);
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 7, out.as.i);
  MsObjTuple* t2 = msObjTupleNew(&vm, 2);  // 等值异对象 → 同槽改值
  EXPECT(ctx, t2 != NULL);
  msVmPush(&vm, msValueObj(&t2->head));
  t2->items[0] = msValueInt(1);
  t2->items[1] = msValueInt(2);
  MsValue k2 = msValueObj(&t2->head);
  op = msObjMapPut(&vm, m, &k2, MV_INT(8));
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 1, m->liveCount);
  op = msObjMapGet(m, &k1, &out);
  EXPECT_EQ(ctx, MS_MAP_OK, op);
  EXPECT_EQ(ctx, 8, out.as.i);
  MsObjList* list = msObjListNew(&vm);  // 含 list 元素 → 整体不可 hash
  EXPECT(ctx, list != NULL);
  msVmPush(&vm, msValueObj(&list->head));
  MsObjTuple* t3 = msObjTupleNew(&vm, 2);
  EXPECT(ctx, t3 != NULL);
  msVmPush(&vm, msValueObj(&t3->head));
  t3->items[0] = msValueObj(&list->head);
  MsValue k3 = msValueObj(&t3->head);
  op = msObjMapPut(&vm, m, &k3, MV_INT(9));
  EXPECT_EQ(ctx, MS_MAP_UNHASHABLE, op);
  op = msObjMapGet(m, &k3, &out);
  EXPECT_EQ(ctx, MS_MAP_UNHASHABLE, op);
  EXPECT(ctx, !t3->hashValid);  // 不可 hash 不缓存
  msVmFree(&vm);
}

TEST(mapDeepEqualAndRepr) {  // 深相等与插入序无关；repr `{"a": 1, "b": 2}`
  MsVM vm;
  msVmInit(&vm);
  MsObjMap* a = msObjMapNew(&vm);
  EXPECT(ctx, a != NULL);
  msVmPush(&vm, msValueObj(&a->head));
  MsObjMap* b = msObjMapNew(&vm);
  EXPECT(ctx, b != NULL);
  msVmPush(&vm, msValueObj(&b->head));
  MsValue ka = msValueObj(&msInternBytes(&vm, "a", 1)->head);
  MsValue kb = msValueObj(&msInternBytes(&vm, "b", 1)->head);
  EXPECT(ctx, msObjMapPut(&vm, a, &ka, MV_INT(1)) == MS_MAP_OK);  // a = {a:1, b:2}
  EXPECT(ctx, msObjMapPut(&vm, a, &kb, MV_INT(2)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, b, &kb, MV_INT(2)) == MS_MAP_OK);  // b = {b:2, a:1}（序不同）
  EXPECT(ctx, msObjMapPut(&vm, b, &ka, MV_INT(1)) == MS_MAP_OK);
  MsValue mva = msValueObj(&a->head);
  MsValue mvb = msValueObj(&b->head);
  EXPECT(ctx, msValueEqual(&mva, &mvb));
  EXPECT(ctx, msObjMapPut(&vm, b, &ka, MV_INT(3)) == MS_MAP_OK);  // 改一键值
  EXPECT(ctx, !msValueEqual(&mva, &mvb));
  EXPECT(ctx, msObjMapPut(&vm, b, &ka, MV_INT(1)) == MS_MAP_OK);  // 还原
  EXPECT(ctx, msValueEqual(&mva, &mvb));
  MsObjString* r = msValueReprForm(&vm, &mva);
  EXPECT(ctx, r != NULL);
  EXPECT_EQ(ctx, 16, r->byteLen);
  EXPECT(ctx, memcmp(r->bytes, "{\"a\": 1, \"b\": 2}", 16) == 0);
  msVmFree(&vm);
}

TEST(mapGcKeepalive) {  // markChildren：map 根活 → list 值经 seq 链标记存活（误收则 UAF）
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  EXPECT(ctx, list != NULL);
  msVmPush(&vm, msValueObj(&list->head));  // root：map 分配窗口 GC 纪律
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(42)));
  MsObjMap* m = msObjMapNew(&vm);
  EXPECT(ctx, m != NULL);
  msVmPush(&vm, msValueObj(&m->head));
  MsValue k = msValueInt(1);
  MsValue lv = msValueObj(&list->head);
  EXPECT(ctx, msObjMapPut(&vm, m, &k, &lv) == MS_MAP_OK);
  vm.stack[0] = vm.stack[1];  // 撤 list 槽：map 成唯一根（list 只能经 seq 链存活）
  vm.stackTop--;
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 2, gcHeapObjCount(&vm));  // map + list
  MsValue out = msValueNil();
  EXPECT(ctx, msObjMapGet(m, &k, &out) == MS_MAP_OK);
  EXPECT(ctx, ((MsObjList*)out.as.obj) == list);  // 幸存金丝雀：指针不变
  EXPECT_EQ(ctx, 1, ((MsObjList*)out.as.obj)->count);
  msVmFree(&vm);
}

TEST(mapRunSrcSmoke) {  // 编译面前态：AST_MAP 字面量暂不支持（Task 4 接通前文档化现状）
  RunResult r = runSrc("var m = {\"a\": 1}\n");
  EXPECT(ctx, !r.compileOk);
  free(r.out);
}

// ---------- 数据面（Task 3：set = map 特例 + 集合运算） ----------

TEST(setBasicsAndOrder) {  // 增删查/数值塔去重/插入序快照/深相等与形态（02 §5.4）
  MsVM vm;
  msVmInit(&vm);
  MsObjSet* st = msObjSetNew(&vm);
  EXPECT(ctx, st != NULL);
  msVmPush(&vm, msValueObj(&st->head));  // root：源/结果分配窗口 GC 纪律（D6）
  MsValue k1 = msValueInt(1);
  MsValue k2 = msValueInt(2);
  MsValue kf = msValueFloat(1.0);
  EXPECT(ctx, msObjSetAdd(&vm, st, &k1) == MS_MAP_OK);
  EXPECT(ctx, msObjSetAdd(&vm, st, &k2) == MS_MAP_OK);
  EXPECT(ctx, msObjSetAdd(&vm, st, &kf) == MS_MAP_OK);  // 1.0 与 1 同元素（02 §7 数值塔）
  EXPECT(ctx, msObjSetHas(st, &kf));
  EXPECT_EQ(ctx, 2, msObjSetCount(st));
  MsObjList* l = msObjSetToList(&vm, st);
  EXPECT(ctx, l != NULL);
  msVmPush(&vm, msValueObj(&l->head));  // root
  EXPECT_EQ(ctx, 2, l->count);  // 插入序快照：1, 2（1.0 并入首键）
  EXPECT(ctx, msIsI64(&l->items[0]) && l->items[0].as.i == 1);
  EXPECT(ctx, msIsI64(&l->items[1]) && l->items[1].as.i == 2);
  MsObjSet* o = msObjSetNew(&vm);  // {2,1}：插入序无关深相等（22 §4）
  EXPECT(ctx, o != NULL);
  msVmPush(&vm, msValueObj(&o->head));
  EXPECT(ctx, msObjSetAdd(&vm, o, &k2) == MS_MAP_OK);
  EXPECT(ctx, msObjSetAdd(&vm, o, &k1) == MS_MAP_OK);
  MsValue va = msValueObj(&st->head);
  MsValue vo = msValueObj(&o->head);
  EXPECT(ctx, msValueEqual(&va, &vo));
  MsObjString* rf = msValueReprForm(&vm, &va);
  EXPECT(ctx, rf != NULL);
  EXPECT_EQ(ctx, 6, rf->byteLen);
  EXPECT(ctx, memcmp(rf->bytes, "{1, 2}", 6) == 0);
  MsObjSet* e = msObjSetNew(&vm);  // 空集 `set()`（`{}` 为空 map，02 §5.4）
  EXPECT(ctx, e != NULL);
  msVmPush(&vm, msValueObj(&e->head));
  MsValue ve = msValueObj(&e->head);
  MsObjString* ef = msValueReprForm(&vm, &ve);
  EXPECT(ctx, ef != NULL);
  EXPECT_EQ(ctx, 5, ef->byteLen);
  EXPECT(ctx, memcmp(ef->bytes, "set()", 5) == 0);
  EXPECT(ctx, !msValueEqual(&va, &ve));
  EXPECT(ctx, msObjSetRemove(st, &k1) == MS_MAP_OK);
  EXPECT(ctx, !msObjSetHas(st, &k1));
  EXPECT_EQ(ctx, MS_MAP_NOENT, msObjSetRemove(st, &k1));
  MsValue lv = msValueObj(&l->head);  // list 不可为元素（hashable 门控，02 §5.4）
  EXPECT_EQ(ctx, MS_MAP_UNHASHABLE, msObjSetAdd(&vm, st, &lv));
  msVmFree(&vm);
}

TEST(setAlgebra) {  // | & - ^ 运算族（结果序：左序在前）+ copy 独立性（02 §5.4）
  MsVM vm;
  msVmInit(&vm);
  MsObjSet* a = msObjSetNew(&vm);
  EXPECT(ctx, a != NULL);
  msVmPush(&vm, msValueObj(&a->head));  // 源 root（运算族契约：源由调用方 root）
  MsObjSet* b = msObjSetNew(&vm);
  EXPECT(ctx, b != NULL);
  msVmPush(&vm, msValueObj(&b->head));
  MsValue k1 = msValueInt(1);
  MsValue k2 = msValueInt(2);
  MsValue k3 = msValueInt(3);
  EXPECT(ctx, msObjSetAdd(&vm, a, &k1) == MS_MAP_OK);  // a = {1,2}
  EXPECT(ctx, msObjSetAdd(&vm, a, &k2) == MS_MAP_OK);
  EXPECT(ctx, msObjSetAdd(&vm, b, &k2) == MS_MAP_OK);  // b = {2,3}
  EXPECT(ctx, msObjSetAdd(&vm, b, &k3) == MS_MAP_OK);
  MsObjSet* u = msObjSetUnion(&vm, a, b);  // {1,2,3}
  EXPECT(ctx, u != NULL);
  msVmPush(&vm, msValueObj(&u->head));  // 结果 root
  EXPECT_EQ(ctx, 3, msObjSetCount(u));
  MsObjList* ul = msObjSetToList(&vm, u);
  EXPECT(ctx, ul != NULL);
  msVmPush(&vm, msValueObj(&ul->head));
  EXPECT_EQ(ctx, 3, ul->count);  // 序：a 序 1,2 在前，并入 b 新元素 3
  EXPECT(ctx, msIsI64(&ul->items[0]) && ul->items[0].as.i == 1);
  EXPECT(ctx, msIsI64(&ul->items[1]) && ul->items[1].as.i == 2);
  EXPECT(ctx, msIsI64(&ul->items[2]) && ul->items[2].as.i == 3);
  MsObjSet* is = msObjSetIntersect(&vm, a, b);  // {2}
  EXPECT(ctx, is != NULL);
  msVmPush(&vm, msValueObj(&is->head));
  EXPECT_EQ(ctx, 1, msObjSetCount(is));
  EXPECT(ctx, msObjSetHas(is, &k2));
  EXPECT(ctx, !msObjSetHas(is, &k1));
  MsObjSet* d = msObjSetDiff(&vm, a, b);  // {1}
  EXPECT(ctx, d != NULL);
  msVmPush(&vm, msValueObj(&d->head));
  EXPECT_EQ(ctx, 1, msObjSetCount(d));
  EXPECT(ctx, msObjSetHas(d, &k1));
  EXPECT(ctx, !msObjSetHas(d, &k2));
  MsObjSet* s = msObjSetSymDiff(&vm, a, b);  // {1,3}
  EXPECT(ctx, s != NULL);
  msVmPush(&vm, msValueObj(&s->head));
  EXPECT_EQ(ctx, 2, msObjSetCount(s));
  MsObjList* sl = msObjSetToList(&vm, s);
  EXPECT(ctx, sl != NULL);
  msVmPush(&vm, msValueObj(&sl->head));
  EXPECT_EQ(ctx, 2, sl->count);  // 序：a-only（1）后 b-only（3）
  EXPECT(ctx, msIsI64(&sl->items[0]) && sl->items[0].as.i == 1);
  EXPECT(ctx, msIsI64(&sl->items[1]) && sl->items[1].as.i == 3);
  MsObjSet* c = msObjSetCopy(&vm, a);  // 拷贝独立
  EXPECT(ctx, c != NULL);
  msVmPush(&vm, msValueObj(&c->head));
  MsValue k9 = msValueInt(9);
  EXPECT(ctx, msObjSetAdd(&vm, c, &k9) == MS_MAP_OK);
  EXPECT_EQ(ctx, 3, msObjSetCount(c));
  EXPECT_EQ(ctx, 2, msObjSetCount(a));  // 源不被改动
  MsObjSet* ss = msObjSetSymDiff(&vm, a, a);  // 同对象双操作数 → 空集
  EXPECT(ctx, ss != NULL);
  msVmPush(&vm, msValueObj(&ss->head));
  EXPECT_EQ(ctx, 0, msObjSetCount(ss));
  msVmFree(&vm);
}

TEST(setTupleElementsAndHashCache) {  // 等值异对象 tuple 元素去重 + 惰性 hash 缓存 + 数值塔探测
  MsVM vm;
  msVmInit(&vm);
  MsObjSet* st = msObjSetNew(&vm);
  EXPECT(ctx, st != NULL);
  msVmPush(&vm, msValueObj(&st->head));
  MsObjTuple* t1 = msObjTupleNew(&vm, 2);
  EXPECT(ctx, t1 != NULL);
  msVmPush(&vm, msValueObj(&t1->head));  // root：tuple 元素非永久，落栈防误收
  t1->items[0] = msValueInt(1);
  t1->items[1] = msValueInt(2);
  MsValue v1 = msValueObj(&t1->head);
  EXPECT(ctx, msObjSetAdd(&vm, st, &v1) == MS_MAP_OK);
  EXPECT(ctx, t1->hashValid);  // 首次入 set 即缓存（22 §4 惰性，同 map）
  MsObjTuple* t2 = msObjTupleNew(&vm, 2);  // 等值异对象 → 同元素去重
  EXPECT(ctx, t2 != NULL);
  msVmPush(&vm, msValueObj(&t2->head));
  t2->items[0] = msValueInt(1);
  t2->items[1] = msValueInt(2);
  MsValue v2 = msValueObj(&t2->head);
  EXPECT(ctx, msObjSetAdd(&vm, st, &v2) == MS_MAP_OK);
  EXPECT_EQ(ctx, 1, msObjSetCount(st));
  MsObjTuple* t3 = msObjTupleNew(&vm, 2);  // 第三等值 tuple：探测命中
  EXPECT(ctx, t3 != NULL);
  msVmPush(&vm, msValueObj(&t3->head));
  t3->items[0] = msValueInt(1);
  t3->items[1] = msValueInt(2);
  MsValue v3 = msValueObj(&t3->head);
  EXPECT(ctx, msObjSetHas(st, &v3));
  MsValue ki = msValueInt(7);  // 数值塔：int 入表 float 探测命中（02 §7）
  MsValue kf = msValueFloat(7.0);
  EXPECT(ctx, msObjSetAdd(&vm, st, &ki) == MS_MAP_OK);
  EXPECT(ctx, msObjSetHas(st, &kf));
  EXPECT_EQ(ctx, 2, msObjSetCount(st));
  msVmFree(&vm);
}

TEST(setBoolIntDistinct) {  // add(true) 后 has(1) false（02 §2 bool≠int 经探测门控）
  MsVM vm;
  msVmInit(&vm);
  MsObjSet* st = msObjSetNew(&vm);
  EXPECT(ctx, st != NULL);
  msVmPush(&vm, msValueObj(&st->head));
  MsValue kt = msValueBool(true);
  MsValue ki = msValueInt(1);
  EXPECT(ctx, msObjSetAdd(&vm, st, &kt) == MS_MAP_OK);
  EXPECT(ctx, msObjSetHas(st, &kt));
  EXPECT(ctx, !msObjSetHas(st, &ki));
  EXPECT_EQ(ctx, 1, msObjSetCount(st));
  msVmFree(&vm);
}

TEST(setGcKeepalive) {  // setMarkChildren → 内层 map → seq 链：堆 string 元素存活（误收则 UAF）
  MsVM vm;
  msVmInit(&vm);
  MsObjString* str = msObjStringNew(&vm, "x", 1);  // 非 intern 堆对象：可回收
  EXPECT(ctx, str != NULL);
  msVmPush(&vm, msValueObj(&str->head));  // root：set 分配窗口 GC 纪律
  MsObjSet* st = msObjSetNew(&vm);
  EXPECT(ctx, st != NULL);
  msVmPush(&vm, msValueObj(&st->head));
  MsValue sv = msValueObj(&str->head);
  EXPECT(ctx, msObjSetAdd(&vm, st, &sv) == MS_MAP_OK);
  vm.stack[0] = vm.stack[1];  // 撤 string 槽：set 成唯一根（string 只能经 set→map seq 链存活）
  vm.stackTop--;
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 3, gcHeapObjCount(&vm));  // set + 内层 map + string
  EXPECT(ctx, msObjSetHas(st, &sv));
  ms_i64 cur = msObjMapFirst(st->map);
  const MsValue* k = cur >= 0 ? msObjMapKeyAt(st->map, cur) : NULL;
  EXPECT(ctx, k != NULL && k->as.obj == &str->head);  // 幸存金丝雀：指针不变
  EXPECT_EQ(ctx, 1, ((MsObjString*)k->as.obj)->byteLen);
  EXPECT(ctx, ((MsObjString*)k->as.obj)->bytes[0] == 'x');  // 回收后内容可读
  msVmFree(&vm);
}

TEST(setConstructorGcSafety) {  // 构造器双分配窗口：小阈值批量构造不失根不崩溃（D6）
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=4096");
#else
  setenv("MS_GC_HEAP_MAX", "4096", 1);
#endif
  MsVM vm;
  msVmInit(&vm);  // gcHeapInit 读 env
  for (int i = 0; i < 500; i++) {  // 每轮 set+map 双分配，guard 常态触发整堆回收
    MsObjSet* st = msObjSetNew(&vm);
    EXPECT(ctx, st != NULL);
    MsValue k = msValueInt(i);
    EXPECT(ctx, msObjSetAdd(&vm, st, &k) == MS_MAP_OK);
  }
  EXPECT(ctx, gcHeapObjCount(&vm) > 0);
  msVmFree(&vm);
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=");
#else
  unsetenv("MS_GC_HEAP_MAX");
#endif
}

int main(void) {
  TestCtx ctx = {0};
  mapInsertionOrder(&ctx);
  mapNumericTowerKeys(&ctx);
  mapNumericEdgeDomains(&ctx);
  mapUnhashableKey(&ctx);
  mapRehashStress(&ctx);
  mapNaNKeyIEEE(&ctx);
  mapTupleKeys(&ctx);
  mapDeepEqualAndRepr(&ctx);
  mapGcKeepalive(&ctx);
  mapRunSrcSmoke(&ctx);
  setBasicsAndOrder(&ctx);
  setAlgebra(&ctx);
  setTupleElementsAndHashCache(&ctx);
  setBoolIntDistinct(&ctx);
  setGcKeepalive(&ctx);
  setConstructorGcSafety(&ctx);
  if (ctx.failures > 0) {
    fprintf(stderr, "%d test(s) failed\n", ctx.failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
