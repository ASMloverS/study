import "ring"

// new + push + to_list
var r = ring.new(3)
r.push(1)
r.push(2)
r.push(3)
print(r.to_list())
// expect: [1, 2, 3]

// is_full / capacity / size
print(r.is_full())
// expect: true
print(r.capacity())
// expect: 3
print(r.size())
// expect: 3

// push when full evicts oldest
var evicted = r.push(4)
print(evicted)
// expect: 1
print(r.to_list())
// expect: [2, 3, 4]

// pop
print(r.pop())
// expect: 2
print(r.size())
// expect: 2

// peek
print(r.peek())
// expect: 3

// is_empty
var r2 = ring.new(2)
print(r2.is_empty())
// expect: true
r2.push(10)
print(r2.is_empty())
// expect: false

// clear
r2.push(20)
r2.clear()
print(r2.size())
// expect: 0
print(r2.is_empty())
// expect: true

// wrap-around
var r3 = ring.new(3)
r3.push(1)
r3.push(2)
r3.push(3)
r3.pop()
r3.push(4)
print(r3.to_list())
// expect: [2, 3, 4]
