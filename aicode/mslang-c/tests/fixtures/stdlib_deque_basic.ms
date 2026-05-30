import "deque"

// new + push_back + to_list
var d = deque.new()
d.push_back(1)
d.push_back(2)
d.push_front(0)
print(d.to_list())
// expect: [0, 1, 2]

// size / is_empty
print(d.size())
// expect: 3
print(d.is_empty())
// expect: false

// pop_front
print(d.pop_front())
// expect: 0

// pop_back
print(d.pop_back())
// expect: 2

// peek_front / peek_back
print(d.peek_front())
// expect: 1
print(d.peek_back())
// expect: 1

// get
var d2 = deque.from_list([10, 20, 30])
print(d2.get(0))
// expect: 10
print(d2.get(2))
// expect: 30

// clear
d2.clear()
print(d2.is_empty())
// expect: true
print(d2.size())
// expect: 0

// from_list
var d3 = deque.from_list([7, 8, 9])
print(d3.to_list())
// expect: [7, 8, 9]
