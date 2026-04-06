# Task 03: Hash Table

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement open-addressing hash table with `MsObjString*` keys, used for globals, string interning, and method tables.
**Dependencies:** T02
**Produces:** `MsTable` with insert/get/delete/find_string; unit tests passing

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/table.h` | MsTable 定义和 API |
| Create | `src/table.c` | 哈希表实现 |
| Create | `tests/unit/test_table.c` | 单元测试 |

## Key Data Structures / API

```c
// include/ms/table.h
#pragma once
#include "ms/value.h"

typedef struct MsObjString MsObjString;

// Tombstone sentinel — non-NULL invalid pointer for deleted slots
#define MS_TABLE_TOMBSTONE ((MsObjString*)(uintptr_t)1)

typedef struct {
    MsObjString* key;  // NULL=empty, TOMBSTONE=deleted
    MsValue value;
} MsEntry;

typedef struct {
    MsEntry* entries;
    int count;      // live + tombstones (used for load factor)
    int live_count; // live entries only
    int capacity;   // always power of 2
} MsTable;

void         ms_table_init(MsTable* t);
void         ms_table_free(MsTable* t);
bool         ms_table_set(MsTable* t, MsObjString* key, MsValue val);  // true if new key
bool         ms_table_get(MsTable* t, MsObjString* key, MsValue* out);
bool         ms_table_delete(MsTable* t, MsObjString* key);
void         ms_table_add_all(MsTable* dst, MsTable* src);
MsObjString* ms_table_find_string(MsTable* t, const char* chars, int length, uint32_t hash);
void         ms_table_remove_white(MsTable* t);  // GC: remove unmarked string entries
```

## Implementation Notes

- **哈希**: 使用 ObjString 内置的 `hash` 字段 (FNV-1a, 在 T04 实现)
- **探测**: 线性探测, `index = hash & (capacity - 1)`
- **负载因子**: count (含 tombstone) > capacity * 3/4 时扩容
- **容量**: 始终为 2 的幂, 初始容量 0 (首次插入时分配 8)
- **Tombstone**: 删除时 key 设为 `MS_TABLE_TOMBSTONE`, value 设为 `MS_BOOL_VAL(true)` (哨兵). 插入时可复用 tombstone 槽位
- **find_string**: 按字符内容和长度查找 (不是指针比较), 用于字符串驻留. 比较前先检查 hash 是否匹配
- **remove_white**: GC 清扫阶段调用, 移除未标记的字符串 (用于清理驻留表)

```c
// 核心查找逻辑
static MsEntry* find_entry(MsEntry* entries, int capacity, MsObjString* key) {
    uint32_t index = key->hash & (uint32_t)(capacity - 1);
    MsEntry* tombstone = NULL;
    for (;;) {
        MsEntry* entry = &entries[index];
        if (entry->key == NULL) {
            return tombstone != NULL ? tombstone : entry;  // empty slot
        } else if (entry->key == MS_TABLE_TOMBSTONE) {
            if (tombstone == NULL) tombstone = entry;
        } else if (entry->key == key) {
            return entry;  // found (interned string → pointer compare)
        }
        index = (index + 1) & (uint32_t)(capacity - 1);
    }
}
```

## C Unit Tests

```c
// tests/unit/test_table.c
// 注意: 此测试需要 T04 的 MsObjString 才能完整运行
// 可先用 mock string 结构测试基础逻辑, 或在 T04 完成后一起测试

#include "test_assert.h"
#include "ms/table.h"

// 简易 mock — 真实实现在 T04 后替换
typedef struct {
    MsObject obj;
    uint32_t hash;
    int length;
    char data[32];
} MockString;

static MockString make_mock(const char* s, uint32_t h) {
    MockString m = {0};
    m.obj.type = MS_OBJ_STRING;
    m.hash = h;
    m.length = (int)strlen(s);
    strncpy(m.data, s, 31);
    return m;
}

static void test_set_get(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k1 = make_mock("x", 100);
    ms_table_set(&t, (MsObjString*)&k1, MS_INT_VAL(42));
    MsValue out;
    TEST_ASSERT(ms_table_get(&t, (MsObjString*)&k1, &out));
    TEST_ASSERT(MS_AS_INT(out) == 42);
    ms_table_free(&t);
}

static void test_delete(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k1 = make_mock("a", 200);
    ms_table_set(&t, (MsObjString*)&k1, MS_INT_VAL(1));
    TEST_ASSERT(ms_table_delete(&t, (MsObjString*)&k1));
    MsValue out;
    TEST_ASSERT(!ms_table_get(&t, (MsObjString*)&k1, &out));
    ms_table_free(&t);
}

static void test_grow(void) {
    MsTable t;
    ms_table_init(&t);
    MockString keys[20];
    for (int i = 0; i < 20; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "k%d", i);
        keys[i] = make_mock(buf, (uint32_t)(i * 7 + 1));
        ms_table_set(&t, (MsObjString*)&keys[i], MS_INT_VAL(i));
    }
    TEST_ASSERT(t.live_count == 20);
    TEST_ASSERT(t.capacity >= 20);
    MsValue out;
    TEST_ASSERT(ms_table_get(&t, (MsObjString*)&keys[15], &out));
    TEST_ASSERT(MS_AS_INT(out) == 15);
    ms_table_free(&t);
}

int main(void) {
    test_set_get();
    test_delete();
    test_grow();
    printf("test_table: all passed\n");
    return 0;
}
```

## .ms Integration Tests

无直接测试 — 哈希表是内部数据结构, 通过变量/全局/类方法表间接测试。
