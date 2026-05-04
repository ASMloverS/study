// purpose : monomorphic inline cache hit — stresses OP_GET_PROPERTY with single shape
// hot-op  : OP_GET_PROPERTY (IC hit)
// expected: 20000000
var N = 20000000
class Point {
    init(x, y) {
        this.x = x
        this.y = y
    }
}
var p = Point(1, 0)
var sum = 0
var i = 0
while (i < N) {
    sum = sum + p.x
    i = i + 1
}
print(sum)
