// T016 阶乘 100 精确值（DoD）
var f = 1n
for i in range(2, 101) {
    f = f * i
}
print(f)
