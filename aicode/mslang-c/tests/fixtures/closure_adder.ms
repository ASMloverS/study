fun make_adder(n) {
  return fun(x) { return x + n }
}
var add5 = make_adder(5)
var add10 = make_adder(10)
print(add5(3))
// expect: 8
print(add10(3))
// expect: 13
