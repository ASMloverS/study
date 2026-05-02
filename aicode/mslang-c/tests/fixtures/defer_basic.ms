fun test() {
  defer fun() { print("deferred") }
  print("first")
}
test()
// expect: first
// expect: deferred
