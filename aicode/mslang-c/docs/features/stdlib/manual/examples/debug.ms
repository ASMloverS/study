import "debug"

// typeof：获取值的类型名称
print(debug.typeof(nil))        // nil
print(debug.typeof(true))       // bool
print(debug.typeof(42))         // int
print(debug.typeof(3.14))       // number（注意：非 "num"）
print(debug.typeof("hello"))    // string
print(debug.typeof([1,2,3]))    // list
var m = {"a": 1}
print(debug.typeof(m))          // map

// is_native / is_closure
fun add(a, b) { return a + b }
print(debug.is_native(len))    // true  (len 是内置 native)
print(debug.is_closure(add))   // true
print(debug.is_native(add))    // false

// frame_info：当前帧信息
fun show_frame() {
    var info = debug.frame_info()
    print(info["name"])        // show_frame
    print(info["line"] > 0)    // true
}
show_frame()

// traceback：返回调用栈字符串
fun inner() {
    var tb = debug.traceback()
    print(len(tb) > 0)   // true
}
fun outer() { inner() }
outer()

// disasm：反汇编函数字节码
fun square(x) { return x * x }
var asm_str = debug.disasm(square)
print(len(asm_str) > 0)   // true

// vm_stats：虚拟机/GC 统计
var stats = debug.vm_stats()
print(stats["gc_bytes"] >= 0)    // true
print(stats["gc_threshold"] > 0) // true
print(stats["frame_used"] > 0)   // true
