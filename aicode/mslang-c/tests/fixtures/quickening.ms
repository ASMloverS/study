// Integer arithmetic (should quicken to _II variants)
var sum = 0
for (var i = 0; i < 100; i = i + 1) {
  sum = sum + i
}
print(sum)
// expect: 4950

// Float arithmetic (should quicken to _FF)
var fsum = 0.0
for (var i = 0; i < 10; i = i + 1) {
  fsum = fsum + 0.1
}
print(fsum > 0.9)
// expect: true

// String concat (should quicken to _SS)
var s = ""
for (var i = 0; i < 5; i = i + 1) {
  s = s + "x"
}
print(s)
// expect: xxxxx

// Deopt: same function called with different types
fun add(a, b) { return a + b }
print(add(1, 2))
// expect: 3
print(add(1.5, 2.5))
// expect: 4
print(add("hello", " world"))
// expect: hello world

// Comparison quickening
fun less(a, b) { return a < b }
print(less(1, 2))
// expect: true
print(less(2.0, 1.0))
// expect: false
