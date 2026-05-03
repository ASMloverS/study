fun test() {
  var a = nil
  var b = nil
  var c = nil
  return a
}
print(test())
// expect: nil

fun negation() {
  return -42
}
print(negation())
// expect: -42

fun dead_code() {
  return 1
  print("unreachable")
}
print(dead_code())
// expect: 1

fun fib(n) {
  if (n <= 1) return n
  return fib(n-1) + fib(n-2)
}
print(fib(10))
// expect: 55
