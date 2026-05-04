// purpose : polymorphic inline cache (3-way) — stresses OP_GET_PROPERTY with multiple shapes
// hot-op  : OP_GET_PROPERTY (polymorphic IC)
// expected: 48000000
var N = 8000000
class A { init() { this.val = 1 } }
class B { init() { this.val = 2 } }
class C { init() { this.val = 3 } }
fun get_val(obj) { return obj.val }
var a = A()
var b = B()
var c = C()
var sum = 0
var i = 0
while (i < N) {
    sum = sum + get_val(a)
    sum = sum + get_val(b)
    sum = sum + get_val(c)
    i = i + 1
}
print(sum)
