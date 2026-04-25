class A { init() { this.val = 1 } }
class B { init() { this.val = 2 } }
class C { init() { this.val = 3 } }

fun get_val(obj) { return obj.val }
print(get_val(A()))
// expect: 1
print(get_val(B()))
// expect: 2
print(get_val(C()))
// expect: 3
