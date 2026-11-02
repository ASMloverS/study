// T018 多返回值（05 §5）：return a, b 即 tuple；调用侧赋值式解构接收（含 *rest 与循环内早返回）
func swap(a, b) { return b, a }
var x = 0
var y = 0
x, y = swap(1, 2)
print(x, y)

func trio() { return 1, 2, 3 }
a, *mid, z = trio()
print(a, z, mid)

func findNegative(xs) {
    for v in xs {
        if v < 0 { return v, true }
    }
    return nil, false
}
var n = 0
var found = false
n, found = findNegative([3, 7, -2, -5])
print(n, found)
n, found = findNegative([1, 2])
print(n, found)
