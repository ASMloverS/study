var a = [3, 1, 2]
print(a.len())
a.push(4)
print(a.len())
print(a.pop())
print(a.contains(2))
print(a.index_of(1))
a.sort()
print(a[0])
a.reverse()
print(a[0])

var doubled = [1,2,3].map(fun(x) { return x * 2 })
print(doubled[0])
print(doubled[2])

var evens = [1,2,3,4,5].filter(fun(x) { return x % 2 == 0 })
print(evens.len())

print([1,2,3].join("-"))
