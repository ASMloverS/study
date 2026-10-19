// T011 控制流综合（04 §3 if / §4 for-while / §5 switch 全示例形态）
var x = 7
if x > 0 {
    print("pos")
} else if x == 0 {
    print("zero")
} else {
    print("neg")
}

var s = 0
for i := 0; i < 5; i++ {
    s += i
}
print(s)

var total = 0
for v in range(1, 6) {
    total += v
}
print(total)

var n = 5
while n > 0 {
    n -= 1
}
print(n)

var k = 0
for {
    k += 1
    if k >= 4 { break }
}
print(k)

for i := 0; i < 3; i++ {
    if i == 1 { continue }
    print(i)
}

switch s {
case 4, 10:
    print("ten")
default:
    print("other")
}

switch type(s) {
case string:
    print("str")
case int:
    print("int")
default:
    print("?")
}

var bound = "?"
switch type("ms") {
case int:
    bound = "int"
case string as sv:
    bound = sv
}
print(bound)
