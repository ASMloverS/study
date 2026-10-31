var it = iter([1, 2, 3])
print(next(it))
print(next(it))
print(it.next())
print(next(it), next(it))
for v in iter("ab") {
    print(v)
}
for v in range(10).take(7).drop(2).toList() {
    print(v)
}
print(range(4).map(abs).toList())
print(sum(range(5)))
for k in {"a": 1, "b": 2} {
    print(k)
}
