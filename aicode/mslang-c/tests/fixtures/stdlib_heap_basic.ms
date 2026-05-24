import "heap"

// min-heap basic usage
var h = heap.new()
h.push(5)
h.push(1)
h.push(3)
print(h.pop())   // 1
print(h.pop())   // 3
print(h.pop())   // 5

// max-heap (reversed comparator)
fun max_cmp(a,b) { if (b < a) { return -1 } if (b > a) { return 1 } return 0 }
var maxh = heap.new(max_cmp)
maxh.push(5)
maxh.push(1)
maxh.push(9)
print(maxh.pop())  // 9

// heapify
var h2 = heap.heapify([4, 2, 7, 1, 5])
print(h2.pop())  // 1
print(h2.pop())  // 2

// peek does not remove
var h3 = heap.new()
h3.push(10)
h3.push(3)
print(h3.peek())  // 3
print(h3.size())  // 2

// to_sorted_list
var h4 = heap.new()
h4.push(9)
h4.push(2)
h4.push(5)
h4.push(1)
var sorted = h4.to_sorted_list()
print(sorted[0])  // 1
print(sorted[1])  // 2
print(sorted[2])  // 5
print(sorted[3])  // 9

// nsmallest / nlargest
print(heap.nsmallest(3, [5,2,8,1,9,3]))  // [1,2,3]
print(heap.nlargest(2, [5,2,8,1,9,3]))   // [9,8]
