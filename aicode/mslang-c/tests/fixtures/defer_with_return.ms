fun test() {
  defer fun() { print("cleanup") }
  return 42
}
print(test())
// expect: cleanup
// expect: 42
