// purpose : upvalue read/write — stresses OP_GET_UPVAL / OP_SET_UPVAL
// hot-op  : OP_GET_UPVAL, OP_SET_UPVAL
// expected: 10000000
var N = 10000000
fun make_counter() {
    var count = 0
    fun inc() {
        count = count + 1
        return count
    }
    return inc
}
var c = make_counter()
var last = 0
var i = 0
while (i < N) {
    last = c()
    i = i + 1
}
print(last)
