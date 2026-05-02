fun test() {
  defer fun() { print("A") }
  defer fun() { print("B") }
  defer fun() { print("C") }
  print("body")
}
test()
// expect: body
// expect: C
// expect: B
// expect: A
