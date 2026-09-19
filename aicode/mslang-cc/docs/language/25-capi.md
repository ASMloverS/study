# 25 C API 与扩展模块

对齐 Python C API 的定位（嵌入 + 扩展），但 GC 为追踪式 → **handle rooting** 模型（非借用引用计数）。公共头唯一：`include/ms.h`。

## 1. 设计约束

- C11，`#ifdef __cplusplus` 兼容 C++ 包含。
- 稳定 ABI：`MS_API_VERSION` 门禁；所有不透明类型经指针访问；struct 尺寸版本化（尾部保留）。
- 命名：`ms` 前缀 + lowerCamelCase（`msNewVM`）；类型 `Ms` 前缀 UpperCamelCase（`28-style-c.md`）。

## 2. 生命周期与执行

```c
ms_VMConfig cfg = msDefaultConfig();
cfg.printStackTrace = true;
ms_VM* vm = msNewVM(&cfg);                 /* 可注入自定义分配器/打印钩子 */

ms_RunOpts opts = msDefaultRunOpts();
if (msRunFile(vm, "app.ms", &opts, NULL) != MS_OK) {
    /* 见 §5 错误处理 */
}
msFreeVM(vm);                              /* 释放全部对象与句柄 */
```

- `msRunString` / `msRunFile` / `msCompile`（→ ms_Chunk*，可序列化 .msc）/ `msRunChunk`。
- 一个进程可多 VM（各自堆与调度器）；**一个 VM 多线程**：C API 调用内部持 VM 大锁（v1 粗粒度，文档注明性能影响；脚本层 goroutine 并发不受此锁影响的路径除外——仅 C API 入口串行化）。

## 3. 值构造与读取

```c
ms_Value v1 = msNewInt(vm, 42);
ms_Value v2 = msNewFloat(vm, 3.14);
ms_Value v3 = msNewString(vm, "hi", 2);      /* UTF-8，带字节长度 */
ms_Value v4 = msNewList(vm, 4);
msListSet(vm, v4, 0, v1);
ms_Value out;
if (msCall(vm, callable, args, 2, &out) != MS_OK) { ... }
```

- 构造族：`msNewInt/msNewBigInt/msNewFloat/msNewBool/msNil/msNewString/msNewStringFmt/msNewList/msNewMap/msNewSet/msNewTuple/msNewChan`。
- 读取族：`msGetInt/msGetFloat/msGetBool/msStringPtrLen/msListGet/msListLen/msMapGet/msMapSet/msMapLen`。
- 判型族：`msIsInt/msIsFloat/msIsString/msIsList/.../msTypeName`。
- `msValueEqual`（== 语义）、`msValueHash`、`msValueRepr`。
- **返回的 ms_Value 是裸值**：仅本次 C 调用内安全（GC 可能移动/回收）——跨调用持有必须 rooting（§4）。

## 4. handle rooting（追踪式 GC 的 C 侧根）

```c
ms_Handle h1 = msRoot(vm, msNewList(vm, 0));   /* 注册表持有，GC 可达 */
ms_Value  lv  = msFromHandle(vm, h1);          /* 每次取用即安全引用 */
msUnroot(vm, h1);                              /* 离开作用域解除 */

/* 块级自动根栈（推荐）： */
msScopePush(vm);
ms_Handle h = msRootScoped(vm, obj);           /* 随 scope 释放 */
msScopePop(vm);                                /* 批量解除 */
```

- 注册表为 GC root（`23-gc.md`）；handle 是稳定槽索引（非指针），对象移动安全。
- 惯例：扩展函数内局部使用免 root（VM 保证当前 C 帧活跃期间 参数与返回值 保活）；存全局/跨调用必 root。

## 5. 错误处理（返回值 + 错误对象查询）

```c
if (msCall(vm, fn, args, 1, &out) != MS_OK) {
    ms_Value err;                              /* 异常对象（脚本 throw 的值） */
    const char* tb;                            /* traceback 文本 */
    msFetchError(vm, &err, &tb);               /* 取走并清除 */
    return MS_ERROR;                           /* 或转换处理 */
}
/* 主动抛错：构造脚本可见异常 */
return msRaiseValueError(vm, "expected positive n, got %d", n);
```

- 每线程错误槽（VM 级 v1 单槽 + API 锁保证一致）；`msHasError/msFetchError/msClearError`。
- 嵌入主循环典型：错误取出后转宿主日志/策略。

## 6. 扩展模块（动态加载）

- 产物命名：`foo.msext.dll` / `foo.msext.so`（`import "foo"` 未命中 .ms 时按 §7 顺序尝试加载）。
- 入口约定：

```c
/* foo.c —— 编译为 foo.msext */
#include "ms.h"

static ms_Value mySqrt(ms_VM* vm, const ms_Value* args, int argc) {
    MS_EXPECT_ARGC(vm, argc, 1);
    ms_Value x = MS_EXPECT_NUMBER(vm, args[0]);
    return msNewFloat(vm, sqrt(msGetFloat(vm, x)));
}

MS_EXPORT int msInit(ms_VM* vm, int apiVersion) {
    if (apiVersion != MS_API_VERSION) return MS_ABI_MISMATCH;
    msRegisterFunc(vm, "mySqrt", mySqrt);      /* 注册进当前模块命名空间 */
    msRegisterConst(vm, "VERSION", msNewString(vm, "1.0", 3));
    return MS_OK;
}
```

- 校验宏：`MS_EXPECT_ARGC / MS_EXPECT_STRING / MS_EXPECT_NUMBER / MS_EXPECT_LIST`（不符自动抛 TypeError 并返回 MS_ERROR）。
- 加载时 ABI 版本不符 → ImportError，进程不崩。
- 注册面：`msRegisterFunc / msRegisterConst / msRegisterClass`（原生类：方法表 + dunder 桥）/ `msRegisterModule`（子模块）。

## 7. 模块搜索顺序（扩展）

`*.ms` 未命中后：`<name>.msext.<platform-ext>` 依 `09-modules.md` §4 同一顺序查找；命中即 dlopen/LoadLibrary + `msInit`。

## 8. 原生函数签名

```c
typedef ms_Value (*ms_NativeFn)(ms_VM* vm, const ms_Value* args, int argc);
```

- 参数值指针仅调用期间有效；需保留 → root。
- 返回 `msNil(vm)` 表示 nil；抛错走 §5。

## 9. 嵌入示例（完整）

```c
#include <stdio.h>
#include "ms.h"

int main(void) {
    ms_VM* vm = msNewVM(NULL);
    msScopePush(vm);
    ms_Handle mod = msRoot(vm, msRunString(vm,
        "func twice(x) { return x * 2 }", NULL));   /* 模块对象 */
    ms_Handle fn = msRoot(vm, msGetGlobal(vm, msFromHandle(vm, mod), "twice"));
    ms_Value arg = msNewInt(vm, 21), out;
    msCall(vm, msFromHandle(vm, fn), &arg, 1, &out);
    printf("%lld\n", (long long)msGetInt(vm, out)); /* 42 */
    msScopePop(vm);
    msFreeVM(vm);
    return 0;
}
```

## 10. 线程与安全规约

- C API 调用须持 VM（内部 API 锁 v1）；不同 VM 可真并行。
- 扩展内不得缓存裸 `ms_Value`/对象指针跨调用；一律 handle。
- 扩展可调 `msYield(vm)`（长任务协作让出）。

## 11. 分发

- ms.h + libms（.lib/.a/.so/.dylib）随工具链发布；扩展构建仅需 ms.h + import lib。
- `ms pkg` 可分发 msext（二进制按平台后缀约定，`27-toolchain.md`）。
