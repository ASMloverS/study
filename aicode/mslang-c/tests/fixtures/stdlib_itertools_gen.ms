import "itertools"

// count: infinite arithmetic sequence via coroutine
var co = itertools.count(10, 2)
print(itertools.take_gen(5, co))
// expect: [10, 12, 14, 16, 18]

// cycle: repeats list elements cyclically
var cy = itertools.cycle(["a", "b", "c"])
print(itertools.take_gen(5, cy))
// expect: [a, b, c, a, b]

// repeat(nil): infinite repetition via coroutine
var rp = itertools.repeat("hi", nil)
print(itertools.take_gen(3, rp))
// expect: [hi, hi, hi]

// product
var prod = itertools.product([1, 2], ["a", "b"])
print(len(prod))
// expect: 4
print(prod[0][0])
// expect: 1
print(prod[0][1])
// expect: a
print(prod[3][0])
// expect: 2
print(prod[3][1])
// expect: b

// permutations
var perms = itertools.permutations([1, 2, 3], 2)
print(len(perms))
// expect: 6

// combinations
var combs = itertools.combinations([1, 2, 3, 4], 2)
print(len(combs))
// expect: 6
print(combs[0][0])
// expect: 1
print(combs[0][1])
// expect: 2

// combinations_with_replacement
var cwr = itertools.combinations_with_replacement([1, 2, 3], 2)
print(len(cwr))
// expect: 6
