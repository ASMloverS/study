var val = 0
fun setter(v) { val = v }
fun getter() { return val }
setter(42)
print(getter())
// expect: 42
