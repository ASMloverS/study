// T018 默认参/rest 参/调用 * 展开（05 §1）：默认值定义处求值；rest 收集余量 list；展开恒末位
func greet(name, prefix = "Mr.") {
    return "${prefix} ${name}"
}
print(greet("Ada"))
print(greet("Ada", "Dr."))

var k = 10
func biased(a, b = k) -> a + b
k = 100
print(biased(1))

func varargs(fmt, *args) {
    return [fmt, args]
}
print(varargs(1))
print(varargs(1, 2))
print(varargs(1, 2, 3))

func mixed(a, b = 2, *rest) {
    return [a, b, rest]
}
print(mixed(1))
print(mixed(1, 5))
print(mixed(1, 5, 6, 7))

func add3(a, b, c) -> a + b + c
var xs = [1, 2, 3]
print(add3(*xs))
print(add3(1, *[2, 3]))

func fill(a, b = 5, c = 6) -> [a, b, c]
print(fill(*[1]))
print(fill(1, *[9]))

print(*xs)
