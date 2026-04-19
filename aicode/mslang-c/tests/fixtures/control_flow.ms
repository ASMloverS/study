if (true) {
  print("yes")
}
// expect: yes

if (false) {
  print("no")
} else {
  print("else")
}
// expect: else

var i = 0
while (i < 3) {
  print(i)
  i = i + 1
}
// expect: 0
// expect: 1
// expect: 2

for (var j = 10; j < 13; j = j + 1) {
  print(j)
}
// expect: 10
// expect: 11
// expect: 12

fun add(a, b) {
  return a + b
}
print(add(3, 4))
// expect: 7

fun fib(n) {
  if (n <= 1) return n
  return fib(n - 1) + fib(n - 2)
}
print(fib(8))
// expect: 21

switch (2) {
  case 1: print("one")
  case 2: print("two")
  default: print("other")
}
// expect: two

var x = 15
if (x > 10) {
  if (x > 20) {
    print("very big")
  } else {
    print("medium")
  }
} else {
  print("small")
}
// expect: medium
