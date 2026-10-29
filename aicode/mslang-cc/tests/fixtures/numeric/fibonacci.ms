// T016 fib(500) 精确值（DoD）
var a = 0n
var b = 1n
for i in range(1, 501) {
    var t = a + b
    a = b
    b = t
}
print(a)
