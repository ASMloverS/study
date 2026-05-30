import "cmp"
import "sort"

// compare
print(cmp.compare(1, 2))
// expect: -1
print(cmp.compare(2, 1))
// expect: 1
print(cmp.compare(3, 3))
// expect: 0

// less / greater / equal
print(cmp.less(1, 2))
// expect: true
print(cmp.greater(3, 2))
// expect: true
print(cmp.equal(4, 4))
// expect: true

// min / max
print(cmp.min(3, 7))
// expect: 3
print(cmp.max(3, 7))
// expect: 7

// clamp
print(cmp.clamp(15, 0, 10))
// expect: 10
print(cmp.clamp(-3, 0, 10))
// expect: 0
print(cmp.clamp(5, 0, 10))
// expect: 5

// natural
var nat = cmp.natural()
print(nat(1, 2))
// expect: -1

// reversed
var rev = cmp.reversed(cmp.compare)
print(rev(1, 2))
// expect: 1

// by_key
var by_len = cmp.by_key(fun(s) { return s.len() })
print(by_len("hi", "hello"))
// expect: -1

// chain
var data = [{"name": "Bob", "age": 30}, {"name": "Alice", "age": 25}, {"name": "Carol", "age": 30}]
var by_age_then_name = cmp.chain([
    cmp.by_key(fun(p) { return p["age"]  }),
    cmp.by_key(fun(p) { return p["name"] })
])
sort.sort(data, nil, false, by_age_then_name)
print(data[0]["name"])
// expect: Alice
print(data[1]["name"])
// expect: Bob
print(data[2]["name"])
// expect: Carol

// null_last
var nl = cmp.null_last(cmp.compare)
print(nl(nil, 1))
// expect: 1
print(nl(1, nil))
// expect: -1

// null_first
var nf = cmp.null_first(cmp.compare)
print(nf(nil, 1))
// expect: -1
print(nf(1, nil))
// expect: 1
