import "itertools"

// chain
print(itertools.chain([1, 2], [3, 4], [5]))
// expect: [1, 2, 3, 4, 5]

// take
print(itertools.take(3, [10, 20, 30, 40]))
// expect: [10, 20, 30]

// drop
print(itertools.drop(2, [1, 2, 3, 4, 5]))
// expect: [3, 4, 5]

// flatten
print(itertools.flatten([[1, 2], [3], [4, 5]]))
// expect: [1, 2, 3, 4, 5]

// accumulate (default: sum)
print(itertools.accumulate([1, 2, 3, 4], nil))
// expect: [1, 3, 6, 10]

// map
print(itertools.map(fun(x){ return x * 2 }, [1, 2, 3]))
// expect: [2, 4, 6]

// filter
print(itertools.filter(fun(x){ return x % 2 == 0 }, [1, 2, 3, 4, 5]))
// expect: [2, 4]

// take_while
print(itertools.take_while(fun(x){ return x < 4 }, [1, 2, 3, 4, 5]))
// expect: [1, 2, 3]

// drop_while
print(itertools.drop_while(fun(x){ return x < 4 }, [1, 2, 3, 4, 5]))
// expect: [4, 5]

// reduce
print(itertools.reduce(fun(a, b){ return a + b }, [1, 2, 3, 4], 0))
// expect: 10

// repeat (finite)
print(itertools.repeat("x", 3))
// expect: [x, x, x]

// zip_iter: access by index (nested lists print as <object>)
var z = itertools.zip_iter([1, 2, 3], ["a", "b", "c"])
print(len(z))
// expect: 3
print(z[0][0])
// expect: 1
print(z[0][1])
// expect: a
print(z[2][1])
// expect: c

// zip_iter truncates to shortest
var zs = itertools.zip_iter([1, 2, 3], ["a", "b"])
print(len(zs))
// expect: 2

// zip_longest
var zl = itertools.zip_longest([1, 2, 3], ["a", "b"], nil)
print(len(zl))
// expect: 3
print(zl[2][1])
// expect: nil

// enumerate
var e = itertools.enumerate(["x", "y", "z"], 0)
print(e[0][0])
// expect: 0
print(e[1][1])
// expect: y
print(e[2][0])
// expect: 2

// windowed
var w = itertools.windowed([1, 2, 3, 4], 2)
print(len(w))
// expect: 3
print(w[0][0])
// expect: 1
print(w[0][1])
// expect: 2
print(w[2][0])
// expect: 3

// batch
var b = itertools.batch([1, 2, 3, 4, 5], 2)
print(len(b))
// expect: 3
print(b[0][0])
// expect: 1
print(b[2][0])
// expect: 5

// pairwise
var pw = itertools.pairwise([10, 20, 30])
print(len(pw))
// expect: 2
print(pw[0][0])
// expect: 10
print(pw[0][1])
// expect: 20

// starmap
print(itertools.starmap(fun(a, b){ return a + b }, [[1, 2], [3, 4], [5, 6]]))
// expect: [3, 7, 11]

// groupby
var g = itertools.groupby([1, 1, 2, 2, 3], fun(x){ return x })
print(len(g))
// expect: 3
print(g[0][0])
// expect: 1
print(len(g[0][1]))
// expect: 2
print(g[2][0])
// expect: 3
