import "set"

var s1 = set.new([1, 2, 3, 4])
var s2 = set.new([3, 4, 5, 6])

print(s1.contains(3))         // true
s1.remove(4)
print(s1.size())               // 3

var u = s1.union(s2)
print(u.contains(1))   // true
print(u.contains(5))   // true
print(u.size())        // 5

var inter = s1.intersection(s2)
print(inter.contains(3))   // true
print(inter.size())        // 1

var diff = s1.difference(s2)
print(diff.contains(1))   // true
print(diff.contains(2))   // true
print(diff.size())        // 2

print(s1.is_subset(set.new([1,2,3,99])))  // true
print(s1.equals(set.new([1,2,3])))        // true

var sd = s1.symmetric_difference(s2)
print(sd.contains(1))   // true
print(sd.contains(5))   // true
print(sd.contains(3))   // false
