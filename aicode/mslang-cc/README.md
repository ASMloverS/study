# mslang

mslang（Maple Scripting Language）是一门动态类型脚本语言，纯 C11 实现：源码 → AST → 栈式字节码 → VM 执行。

- 语法外观取 Go：`func / var / const / :=`，无分号，条件无括号
- 运行语义取 Python：一切皆对象、class + 异常、文件模块、batteries-included 标准库
- 并发取 Go：goroutine + channel（select）+ async/await，多线程 M:N 调度；分代 + 并发三色标记 GC
- 工具链：`ms run / repl / fmt / test / build / pkg / doc / dump / debug`

```ms
func fib(n) {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}
print(fib(10))  // 55
```

## 状态

设计阶段，实现未开始。语言设计见 [docs/language/00-overview.md](docs/language/00-overview.md)，实现任务索引见 [docs/tasks/README.md](docs/tasks/README.md)。

## 仓库结构

| 路径 | 内容 |
|------|------|
| `docs/language/` | 语言与实现设计文档（词法/类型/架构/字节码/GC/调度器/标准库/工具链/路线图） |
| `docs/tasks/` | 按依赖顺序拆分的 44 个实现任务（T001-T044） |
