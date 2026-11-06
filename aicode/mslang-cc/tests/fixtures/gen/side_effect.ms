// T019 惰性证明：无 next 即无执行（副作用计数）
var n = 0
func* count() {
    n = n + 1
    yield n
    n = n + 1
    yield n
}
var it = count()
print(n)
print(next(it))
print(next(it), n)
print(next(it))
