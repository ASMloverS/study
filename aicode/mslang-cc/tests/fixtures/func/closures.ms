func counter() {
    var n = 0
    return func() { n += 1; return n }
}
var c1 = counter()
var c2 = counter()
print(c1(), c1(), c2())        // 1 2 1
var fns = []
for i in range(3) { fns.append(func() { return i }) }
print(fns[0](), fns[1](), fns[2]())   // 0 1 2
