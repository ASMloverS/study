import "sort"

var nums = [3, 1, 4, 1, 5, 9, 2, 6]

// sort in place
sort.sort(nums)
print(nums)  // [1, 1, 2, 3, 4, 5, 6, 9]

// sorted (copy)
var orig = [5, 3, 1]
var copy = sort.sorted(orig)
print(orig)  // [5, 3, 1]
print(copy)  // [1, 3, 5]

// reverse
var desc = sort.sorted([3, 1, 4], nil, true)
print(desc)  // [4, 3, 1]

// key function
var words = ["banana", "apple", "fig"]
sort.sort(words, fun(w){ return len(w) })
print(words[0])  // fig

// stable_sort preserves equal-key order
var pairs = [[2, "b"], [1, "second"], [1, "first"]]
sort.stable_sort(pairs, fun(p){ return p[0] })
print(pairs[0][1])  // second
print(pairs[1][1])  // first

// is_sorted
print(sort.is_sorted([1, 2, 3]))       // true
print(sort.is_sorted([3, 1, 2]))       // false
print(sort.is_sorted_desc([3, 2, 1]))  // true

// binary_search
print(sort.binary_search([1, 2, 3, 4, 5], 3))  // 2
print(sort.binary_search([1, 2, 3, 4, 5], 6))  // -1

// bisect_left / bisect_right
print(sort.bisect_left([1, 3, 3, 5], 3))   // 1
print(sort.bisect_right([1, 3, 3, 5], 3))  // 3

// search_range
var r = sort.search_range([1, 3, 3, 3, 5], 3)
print(r[0])  // 1
print(r[1])  // 4
