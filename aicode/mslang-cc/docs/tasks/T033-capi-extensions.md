# T033 — ms.h C API 与扩展加载

- 阶段: P10 C API + Tier1（roadmap 期 10）
- 依赖: T027、T028
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/25-capi.md` 全文

## 1. 目标与范围

实现公共头 `include/ms.h` 与 C API：VM 生命周期与配置、执行族（msRunString/msRunFile/msCompile/msRunChunk）、值构造/读取/判型族、msCall、错误槽（msHasError/msFetchError/msRaise* 族）、handle rooting（msRoot/msFromHandle/msUnroot + scope 栈）、msext 动态扩展加载（foo.msext.dll/.so：dlopen/LoadLibrary + msInit ABI 校验 + 注册面 + 校验宏）。出口：roadmap 期 10 前半——25 §9 嵌入示例运行 + 示例扩展加载。

不做：原生类注册 msRegisterClass 的 dunder 桥完整化（最小：注册函数/常量/模块；原生类归 T034 按需扩展）、多 VM 并发性能（v1 API 锁串行，文档注明）。

## 2. 实现要点

- ABI（25 §1）：`MS_API_VERSION` 门禁；不透明指针；`#ifdef __cplusplus` 兼容；命名 ms 前缀 lowerCamelCase。
- VM 锁（25 §2）：C API 入口粗粒度锁（v1）；脚本 goroutine 并发不受影响的路径除外。
- 错误处理（25 §5）：每 VM 单错误槽 + API 锁保证一致；msFetchError 取走并清除（含 traceback 文本）；msRaiseValueError 族构造脚本可见异常。
- handle（25 §4）：T027 注册表消费——稳定槽索引；`msRoot/msFromHandle/msUnroot`；块级 `msScopePush/msScopePop` + `msRootScoped`；扩展函数内局部使用免 root（参数与返回值当前 C 帧活跃期保活）。
- 扩展加载（25 §6 §7）：模块搜索在 .ms 未命中后按 09 §4 顺序查 `<name>.msext.<ext>`；入口 `msInit(vm, apiVersion)`：版本不符返回 MS_ABI_MISMATCH → ImportError 不崩；注册面 msRegisterFunc/msRegisterConst/msRegisterModule（msRegisterClass 最小桩）。
- 校验宏（25 §6）：`MS_EXPECT_ARGC/MS_EXPECT_STRING/MS_EXPECT_NUMBER/MS_EXPECT_LIST`。
- 嵌入示例（25 §9）：`examples/embed/embed_basic.c` 入库随 CI 构建。
- 25 §3 的全部构造/读取/判型/相等/hash/repr 接口一次定义齐（部分仅薄包装既有内部实现）。

## 3. 涉及文件

新增: `include/ms.h`、`src/ms_api/ms_api.c`、`src/ms_api/ms_api_ext.c`（动态加载）、`examples/embed/embed_basic.c`、`examples/ext/myext.c`（示例扩展）、`tests/unit/test_capi.c`、`tests/fixtures/extload/*.ms`（加载示例扩展的脚本）
修改: `CMakeLists.txt`（ms.h 安装目标、示例构建、平台动态库后缀定义）、`src/module/module.c`（msext 查找位）

## 4. 验收标准（DoD）

- [ ] 25 §9 嵌入示例编译运行输出 `42`
- [ ] 值族全接口单测通过（构造→读取→判型往返）
- [ ] msCall：调用脚本函数 + 异常路径 msFetchError 取得异常对象与 traceback
- [ ] handle：跨 full GC 存活；scope 栈批量解除
- [ ] 示例扩展 `myext.msext` 被 `import "myext"` 加载；ABI 版本不符 → ImportError（进程不崩）
- [ ] 一个进程双 VM 互不干扰（各自堆）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_capi.c`

| 用例 | 调用 | 期望 |
|------|------|------|
| VM 生命周期 | NewVM→RunString→FreeVM | 无泄漏（ASan） |
| 值往返 | NewInt/NewFloat/NewString/NewList… | 读取一致 |
| msCall | 脚本 twice(x) | out=2x |
| 错误槽 | throw 脚本 + msFetchError | err 非 nil、tb 含行号、取走后清空 |
| msRaise | msRaiseValueError | 脚本 catch 捕获 |
| handle | root 后强制 full GC | 值有效 |
| scope | push/root×N/pop | 批量解除计数 |
| 双 VM | 各跑独立脚本 | 隔离正确 |

### 5.2 ms fixtures `tests/fixtures/extload/`

- `load_ext.ms`：

```ms
import "myext"
print(myext.mySqrt(9.0))    // 3
print(myext.VERSION)        // 1.0
```

- `abi_mismatch.ms`：预置坏版本扩展 → ImportError（stderr 匹配）

## 6. 风险与备注

- Windows 动态库搜索路径：msext 查找按 09 §4 相对模块解析根展开，不依赖进程 PATH——加载用绝对路径 LoadLibrary。
- "扩展内局部免 root"的保活边界依赖调用期间参数保活实现（VM 持调用参数引用直到 native 返回）——单测以"构造-返回"链路 ASan 验证，文档化边界。
