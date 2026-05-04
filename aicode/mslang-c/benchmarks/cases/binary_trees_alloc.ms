// purpose : allocation-heavy + GC pressure — binary tree build and sum
// hot-op  : OP_BUILD_LIST, GC collections
// expected: 262143
var N = 17
fun make(d) {
    if (d == 0) { return [1, nil, nil] }
    return [1, make(d - 1), make(d - 1)]
}
fun check(t) {
    if (t[1] == nil) { return t[0] }
    return t[0] + check(t[1]) + check(t[2])
}
print(check(make(N)))
