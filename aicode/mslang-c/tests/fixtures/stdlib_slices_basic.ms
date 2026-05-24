import "slices"

var nums = [3, 1, 4, 1, 5, 9, 2, 6]

// map
print(slices.map([1,2,3], fun(x){ return x * x }))

// filter
print(slices.filter(nums, fun(x){ return x > 3 }))

// reduce
print(slices.reduce(nums, fun(a,x){ return a+x }, 0))

// any / all
print(slices.any(nums, fun(x){ return x > 8 }))
print(slices.all(nums, fun(x){ return x > 0 }))

// count
print(slices.count(nums, fun(x){ return x % 2 == 0 }))

// unique
print(slices.unique(nums))

// chunk
print(slices.chunk(nums, 3))

// reverse
print(slices.reverse([1,2,3]))

// sorted
print(slices.sorted([3,1,4,1,5]))

// sum / max / min
print(slices.sum(nums))
print(slices.max(nums))
print(slices.min(nums))

// contains / index_of
print(slices.contains(nums, 9))
print(slices.index_of(nums, 9))

// take / drop
print(slices.take(nums, 3))
print(slices.drop(nums, 5))

// enumerate
print(slices.enumerate(["a","b","c"]))
