import "linkedlist"

// new + push_back + to_list
var l = linkedlist.new()
l.push_back(1)
l.push_back(2)
l.push_back(3)
print(l.to_list())
// expect: [1, 2, 3]

// push_front
l.push_front(0)
print(l.to_list())
// expect: [0, 1, 2, 3]

// size / is_empty
print(l.size())
// expect: 4
print(l.is_empty())
// expect: false

// pop_front
print(l.pop_front())
// expect: 0

// pop_back
print(l.pop_back())
// expect: 3

// peek_front / peek_back
print(l.peek_front())
// expect: 1
print(l.peek_back())
// expect: 2

// insert_after
var l2 = linkedlist.new()
var n1 = l2.push_back(1)
var n3 = l2.push_back(3)
l2.insert_after(n1, 2)
print(l2.to_list())
// expect: [1, 2, 3]

// insert_before
var l3 = linkedlist.new()
var na = l3.push_back(1)
var nb = l3.push_back(3)
l3.insert_before(nb, 2)
print(l3.to_list())
// expect: [1, 2, 3]

// remove
var l4 = linkedlist.new()
var x1 = l4.push_back(10)
var x2 = l4.push_back(20)
var x3 = l4.push_back(30)
l4.remove(x2)
print(l4.to_list())
// expect: [10, 30]

// each
var l5 = linkedlist.from_list([4, 5, 6])
l5.each(fun(v){ print(v) })
// expect: 4
// expect: 5
// expect: 6

// from_list
var l6 = linkedlist.from_list([7, 8, 9])
print(l6.size())
// expect: 3
print(l6.to_list())
// expect: [7, 8, 9]
