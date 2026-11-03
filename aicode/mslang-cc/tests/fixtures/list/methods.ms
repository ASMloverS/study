var xs = [3, 1, 2]
xs.insert(1, 9)
print(xs)
xs.remove(9)
print(xs)
print(xs.indexOf(2))
print(xs.contains(5))
xs.sort()
print(xs)
xs.reverse()
print(xs)
print(xs.concat([7, 8]))
print(xs.repeat(2))
print(xs.first())
print(xs.last())
var ys = xs.copy()
ys.append(99)
print(xs)
print(ys)
print([1] + [2])
print([0] * 3)
var ns = [1, 2, 3]
print(ns.map(func(x) { return x * 2 }))
print(ns.filter(func(x) { return x % 2 == 1 }))
print(ns.reduce(func(a, b) { return a + b }))
func scale(xs, k) { return xs.map(func(x) { return x * k }) }
print(scale(ns, 3))
func collect() {
    var out = []
    var src = [1, 2, 3]
    src.each(func(v, i) { out.append(v * v) })
    return out
}
print(collect())
print(sorted(ns, func(x) { return -x }))
