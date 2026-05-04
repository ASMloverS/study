// purpose : method call dispatch — stresses OP_INVOKE
// hot-op  : OP_INVOKE
// expected: 15000000
var N = 15000000
class Counter {
    init() {
        this.n = 0
    }
    inc() {
        this.n = this.n + 1
    }
    get() {
        return this.n
    }
}
var c = Counter()
var i = 0
while (i < N) {
    c.inc()
    i = i + 1
}
print(c.get())
