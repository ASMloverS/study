// T018 同名遮蔽与覆盖（05 §1：无重载，后定义覆盖；内层局部遮蔽外层；函数一等值可整体换绑）
func f() { return 1 }
func f() { return 2 }
print(f())

func outer() {
    var v = 1
    func inner() { var v = 2; return v }
    return inner() + v
}
print(outer())

var g = func(x) -> x * 10
g = func(x) -> x + 100
print(g(1))
