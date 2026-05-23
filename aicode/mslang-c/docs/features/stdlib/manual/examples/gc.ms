import "gc"

// 查询当前存活对象数
print(gc.alive() >= 0)             // true

// 已分配字节数
print(gc.bytes_allocated() >= 0)   // true

// GC 触发阈值
print(gc.threshold() > 0)          // true

// 手动触发完整 GC，返回释放的对象数
var freed = gc.collect()
print(freed >= 0)   // true

// 手动触发次代 GC（仅扫描年轻代）
var freed2 = gc.minor()
print(freed2 >= 0)  // true

// 对象类型计数（map）
var counts = gc.object_counts()
print(counts != nil)   // true

// 调整 GC 阈值（最小 1024）
var old = gc.threshold()
gc.set_threshold(old * 2)
print(gc.threshold() == old * 2)   // true
gc.set_threshold(old)              // 恢复

// 暂停 / 恢复 GC
gc.pause()
print(gc.is_paused())   // true

var strs = []
var i = 0
while (i < 50) {
    strs.push("x" + str(i))
    i = i + 1
}

gc.resume()
print(gc.is_paused())   // false

// 恢复后统计仍可用
print(gc.alive() > 0)            // true
print(gc.bytes_allocated() > 0)  // true
